#include "scene.h"
#include "utils.h"
#include "shader.h"
#include "fbo.h"
#include "texture.h"
#include "application.h"

#include "prefab.h"
#include "extra/cJSON.h"


/************************************************************************************************************/

GTR::Scene* GTR::Scene::instance = NULL;

GTR::Scene::Scene()
{
	phong = false;
	instance = this;
	
}

void GTR::Scene::clear()
{
	for (int i = 0; i < entities.size(); ++i)
	{
		BaseEntity* ent = entities[i];
		delete ent;
	}
	entities.resize(0);
}


void GTR::Scene::addEntity(BaseEntity* entity)
{
	entities.push_back(entity); entity->scene = this;
}

bool GTR::Scene::load(const char* filename)
{
	std::string content;

	this->filename = filename;
	std::cout << " + Reading scene JSON: " << filename << "..." << std::endl;

	if (!readFile(filename, content))
	{
		std::cout << "- ERROR: Scene file not found: " << filename << std::endl;
		return false;
	}

	//parse json string 
	cJSON* json = cJSON_Parse(content.c_str());
	if (!json)
	{
		std::cout << "ERROR: Scene JSON has errors: " << filename << std::endl;
		return false;
	}

	//read global properties
	background_color = readJSONVector3(json, "background_color", background_color);
	ambient_light = readJSONVector3(json, "ambient_light", ambient_light );
	main_camera.eye = readJSONVector3(json, "camera_position", main_camera.eye);
	main_camera.center = readJSONVector3(json, "camera_target", main_camera.center);
	main_camera.fov = readJSONNumber(json, "camera_fov", main_camera.fov);

	//entities
	cJSON* entities_json = cJSON_GetObjectItemCaseSensitive(json, "entities");
	cJSON* entity_json;
	cJSON_ArrayForEach(entity_json, entities_json)
	{
		std::string type_str = cJSON_GetObjectItem(entity_json, "type")->valuestring;
		BaseEntity* ent = createEntity(type_str);
		if (!ent)
		{
			std::cout << " - ENTITY TYPE UNKNOWN: " << type_str << std::endl;
			//continue;
			ent = new BaseEntity();
		}

		addEntity(ent);

		if (cJSON_GetObjectItem(entity_json, "name"))
		{
			ent->name = cJSON_GetObjectItem(entity_json, "name")->valuestring;
			stdlog(std::string(" + entity: ") + ent->name);
		}

		//read transform
		if (cJSON_GetObjectItem(entity_json, "position"))
		{
			ent->model.setIdentity();
			Vector3 position = readJSONVector3(entity_json, "position", Vector3());
			ent->model.translate(position.x, position.y, position.z);
		}

		if (cJSON_GetObjectItem(entity_json, "angle"))
		{
			float angle = cJSON_GetObjectItem(entity_json, "angle")->valuedouble;
			ent->model.rotate(angle * DEG2RAD, Vector3(0, 1, 0));
		}

		if (cJSON_GetObjectItem(entity_json, "rotation"))
		{
			Vector4 rotation = readJSONVector4(entity_json, "rotation");
			Quaternion q(rotation.x, rotation.y, rotation.z, rotation.w);
			Matrix44 R;
			q.toMatrix(R);
			ent->model = R * ent->model;
		}

		if (cJSON_GetObjectItem(entity_json, "target"))
		{
			Vector3 target = readJSONVector3(entity_json, "target", Vector3());
			Vector3 front = target - ent->model.getTranslation();
			ent->model.setFrontAndOrthonormalize(front);
		}

		if (cJSON_GetObjectItem(entity_json, "scale"))
		{
			Vector3 scale = readJSONVector3(entity_json, "scale", Vector3(1, 1, 1));
			ent->model.scale(scale.x, scale.y, scale.z);
		}

		ent->configure(entity_json);
	}

	//free memory
	cJSON_Delete(json);

	return true;
}

void GTR::Scene::updateLights() {
	for (int i = 0; i < lights.size(); i++) {

		lights[i]->lightVisible();

		light_pos[i] = lights[i]->model.getTranslation();
		light_color[i] = lights[i]->color;
		l_intensity[i] = lights[i]->intensity;
		l_cone_angle[i] = (float)cos(lights[i]->cone_angle * DEG2RAD);
		l_max_dist[i] = lights[i]->max_dist;

		l_spotExp[i] = lights[i]->spotExp;
		light_direction[i] = lights[i]->model.frontVector();
		light_type[i] = lights[i]->light_type;
	}
}

/************************************************************************************************************/

GTR::BaseEntity* GTR::Scene::createEntity(std::string type)
{
	if (type == "PREFAB")
		return new GTR::PrefabEntity();
	else if (type == "LIGHT")
		return new GTR::LightEntity();
}

void GTR::BaseEntity::renderInMenu()
{
#ifndef SKIP_IMGUI
	ImGui::Text("Name: %s", name.c_str()); // Edit 3 floats representing a color
	ImGui::Checkbox("Visible", &visible); // Edit 3 floats representing a color
	//Model edit
	ImGuiMatrix44(model, "Model");
#endif
}

/************************************************************************************************************/


GTR::PrefabEntity::PrefabEntity()
{
	entity_type = PREFAB;
	prefab = NULL;
}

void GTR::PrefabEntity::configure(cJSON* json)
{
	if (cJSON_GetObjectItem(json, "filename"))
	{
		filename = cJSON_GetObjectItem(json, "filename")->valuestring;
		prefab = GTR::Prefab::Get( (std::string("data/") + filename).c_str());
	}
}

void GTR::PrefabEntity::renderInMenu()
{
	BaseEntity::renderInMenu();

#ifndef SKIP_IMGUI
	ImGui::Text("filename: %s", filename.c_str()); // Edit 3 floats representing a color
	if (prefab && ImGui::TreeNode(prefab, "Prefab Info"))
	{
		prefab->root.renderInMenu();
		ImGui::TreePop();
	}
#endif
}

/************************************************************************************************************/

GTR::LightEntity::LightEntity() {
	this->entity_type = LIGHT;
	this->colorsaved = false;
	this->intensity = 5.0;
	this->cone_angle = 30.0;
	this->spotExp = 20.0;
	this->max_dist = 5000.0;
	this->bias = 0.001;
	this->show_shadowmap = false;
	this->area_size = 1024.0;
}

GTR::LightEntity::~LightEntity() {

}

void GTR::LightEntity::setColor(vec3 color) {
	this->color = color;
}

void GTR::LightEntity::renderInMenu() {
	BaseEntity::renderInMenu();
#ifndef SKIP_IMGUI
	if (visible) {
		ImGui::Combo("Light Type", (int*)&this->light_type, "POINT\0SPOT\0DIRECTIONAL", 3);
		ImGui::ColorEdit3("Light Color", color.v);
		ImGui::SliderFloat("Max distance", &max_dist, 1.0, 5000.0);
		ImGui::SliderFloat("Intensity", &intensity, 1.0, 50.0);

		if (light_type == GTR::eLightType::SPOT) {
			ImGui::SliderFloat("Cutoff Angle", &cone_angle, 1.0, 80.0);
			ImGui::SliderFloat("Spot Exponent", &spotExp, 0.0, 100.0);
		}
		if (light_type == SPOT || light_type == GTR::eLightType::DIRECTIONAL) {
			ImGui::Checkbox("Show shadowmap", &show_shadowmap);
		}

	}
#endif
}

void GTR::LightEntity::configure(cJSON* json) {

	if (cJSON_GetObjectItem(json, "light_color")) {
		this->color = readJSONVector3(json, "light_color", Vector3());
	}
	if (cJSON_GetObjectItem(json, "light_type")) {
		std::string t = cJSON_GetObjectItem(json, "light_type")->valuestring;
		if (t == "directional" || t == "DIRECTIONAL")
			light_type = GTR::eLightType::DIRECTIONAL;
		else if (t == "point" || t == "POINT")
			light_type = GTR::eLightType::POINT;
		else if (t == "spot" || t == "SPOT")
			light_type = GTR::eLightType::SPOT;
	}
	if (cJSON_GetObjectItem(json, "max_dist")) {
		max_dist = cJSON_GetObjectItem(json, "max_dist")->valuedouble;
	}
	if (cJSON_GetObjectItem(json, "intensity")) {
		intensity = cJSON_GetObjectItem(json, "intensity")->valuedouble;
	}
	if (cJSON_GetObjectItem(json, "cone_angle")) {
		cone_angle = cJSON_GetObjectItem(json, "cone_angle")->valuedouble;
	}
	if (cJSON_GetObjectItem(json, "spot_exp")) {
		spotExp = cJSON_GetObjectItem(json, "spot_exp")->valuedouble;
	}
	if (cJSON_GetObjectItem(json, "target")) {
		Vector3 target = readJSONVector3(json, "target", Vector3(0.0, -0.1, -0.1));
		model.setFrontAndOrthonormalize(target - model.getTranslation());
	}
	if (cJSON_GetObjectItem(json, "area_size")) {
		area_size = cJSON_GetObjectItem(json, "area_size")->valuedouble;
	}

	if (Scene::instance != NULL) {
		Scene::instance->lights.push_back(this);
	}
	cam = new Camera();
	if (light_type == GTR::eLightType::SPOT || light_type == GTR::eLightType::DIRECTIONAL) {
		fbo = new FBO();
		fbo->setDepthOnly(1024 * 2, 1024 * 2);
		if (light_type == GTR::eLightType::SPOT) {
			cam->lookAt(model.getTranslation(), model.getTranslation() + model.frontVector(), Vector3(0, 1, 0));
			cam->setPerspective(cone_angle * 2.0, 1.0, 0.1f, 3000.f);
		}
		else {
			cam->lookAt(model.getTranslation(), model.frontVector(), model.topVector());
			cam->setOrthographic(-area_size, area_size, -area_size, area_size, 0.1f, 3000.f);
			this->bias = 0.01;
		}

	}

}

void gamma_to_linear(Vector3& color) {
	color.x = pow(color.x, INV_GAMMA);
	color.y = pow(color.y, INV_GAMMA);
	color.z = pow(color.z, INV_GAMMA);
}

void GTR::LightEntity::uploadUniforms(Shader*& shader) {
	shader->setUniform("u_light_color", this->color);
	shader->setUniform("u_light_pos", this->model.getTranslation());
	shader->setUniform("u_light_type", light_type);
	shader->setUniform("u_light_maxdist", max_dist);
	shader->setUniform("u_light_direction", model.frontVector());
	shader->setUniform("u_cosCutoff", (float)cos(cone_angle * DEG2RAD));
	if(Scene::instance->phong)
		shader->setUniform("u_light_intensity", intensity/4);
	else
		shader->setUniform("u_light_intensity", intensity);

	shader->setUniform("u_spot_exp", spotExp);

	if (light_type != POINT) {
		shader->setUniform("u_shadow_bias", this->bias);

		//get the depth texture from the FBO
		Texture* shadowmap = fbo->depth_texture;

		//pass it to the shader in slot 8
		shader->setTexture("u_shadowmap", shadowmap, 8);

		//also get the viewprojection from the light
		Matrix44 shadow_proj = cam->viewprojection_matrix;

		//pass it to the shader
		shader->setUniform("u_shadow_viewproj", shadow_proj);
	}
	
}

void GTR::LightEntity::lightVisible() {
	if (!this->visible) {
		if (!this->colorsaved) {
			this->auxcolor = this->color;
			this->color = vec3();
			this->colorsaved = true;
		}
	}
	else {
		if (this->colorsaved) {
			this->color = this->auxcolor;
			this->colorsaved = false;
		}
	}
}

void GTR::LightEntity::orientCam() {
	if (light_type == GTR::eLightType::SPOT) {
		cam->setPerspective(cone_angle * 2.0, 1.0, 0.1f, 3000.f);
		cam->lookAt(model.getTranslation(), model.getTranslation() + model.frontVector(), Vector3(0, 1, 0));
	}
	else if (light_type == GTR::eLightType::DIRECTIONAL) {
		//Vector3 pos = Scene::instance->mainCam->eye;
		cam->lookAt(model.getTranslation(), model.getTranslation() + model.frontVector(), Vector3(0, 1, 0));
	}
}

/************************************************************************************************************/