#include "scene.h"
#include "utils.h"
#include "shader.h"
#include "fbo.h"
#include "texture.h"
#include "application.h"
#include "renderer.h"

#include "prefab.h"
#include "extra/cJSON.h"
#include "extra/hdre.h"


/************************************************************************************************************/

GTR::Scene* GTR::Scene::instance = NULL;

GTR::Scene::Scene()
{
	irradianceEnt = NULL;
	phong = false;
	instance = this;
}

void GTR::Scene::clear(){

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

	//HDRE* hdre = new HDRE();
	//if (hdre->load((cJSON_GetObjectItem(json, "environment")->valuestring)))	//Falta el lector de chars?

	environment = GTR::CubemapFromHDRE("data/night.hdre");

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
	else if (type == "Rprobe")
		return new GTR::reflectionProbeEntity();
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
	useful = false;
}

GTR::LightEntity::~LightEntity() {

}

void GTR::LightEntity::setColor(vec3 color) {
	this->color = color;
}

void GTR::LightEntity::renderInMenu() {
	if (useful == false)
		return;

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
	if (cJSON_GetObjectItem(json, "useful")) {
		const char* str = cJSON_GetObjectItem(json, "useful")->valuestring;
		if (str == "no")
			useful = false;
	}
	else {
		useful = true;
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
void GTR::IrradianceEntity::init() {
	for (int i = 0; i < probes.size(); i++) {
		sProbe probe = probes[i];
		//temporal probe test
		memset(&probe, 0, sizeof(probe));
		probe.pos.set(76, 38, 96);
		probe.sh.coeffs[0].set(1, 0, 0);
	}
}

GTR::IrradianceEntity::IrradianceEntity(){
	active = false;
	probes_texture = NULL;
	size = 2;
	normal_dist = 1.0;
	bool read_irr_info = read("data/irradianceData/irradiance.bin");
	if (!read_irr_info) {
		int fact_dist = 6;
		dimensions = Vector3(12, 10, 12);
		start_pos.set(-60 * fact_dist, 2, -90 * fact_dist);
		end_pos.set(100 * fact_dist, 300, 90 * fact_dist);
		delta = (end_pos - start_pos);

		delta.x /= (dimensions[0] - 1);
		delta.y /= (dimensions[1] - 1);
		delta.z /= (dimensions[2] - 1);
		placeProbes();

	}
}

GTR::IrradianceEntity::~IrradianceEntity(){

}

void GTR::IrradianceEntity::placeProbes() {

	//lets compute the centers
	//pay attention at the order at which we add them
	for (int z = 0; z < dimensions[2]; ++z)
		for (int y = 0; y < dimensions[1]; ++y)
			for (int x = 0; x < dimensions[0]; ++x)
			{
				sProbe p;
				p.local.set(x, y, z);

				//index in the linear array
				p.index = x + y * dimensions[0] + z * dimensions[0] * dimensions[1];

				//and its position
				p.pos = start_pos + delta * Vector3(x, y, z);
				probes.push_back(p);
			}

	init();
}

void GTR::IrradianceEntity::probesToTexture() {


	if (!probes_texture) {
		int p_size = probes.size();
		probes_texture = new Texture(
			9, //9 coefficients per probe
			p_size, //as many rows as probes
			GL_RGB, //3 channels per coefficient
			GL_FLOAT); //they require a high range
	}

	//we must create the color information for the texture. because every SH are 27 floats in the RGB,RGB,... order, we can create an array of SphericalHarmonics and use it as pixels of the texture
	SphericalHarmonics* sh_data =  new SphericalHarmonics[ (dimensions[0] * dimensions[1] * dimensions[2]) ];

	for (int i = 0; i < probes.size(); ++i)
	{
		sh_data[i] = probes[i].sh;
	}

	probes_texture->upload(GL_RGB, GL_FLOAT, false, (uint8*)sh_data);
	//disable any texture filtering when reading
	probes_texture->bind();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	delete[] sh_data;

	active = true;
}

void GTR::IrradianceEntity::uploadUniforms(Shader*& shader){
	shader->setUniform("u_irr_start", start_pos);
	shader->setUniform("u_irr_end", end_pos);
	shader->setUniform("u_irr_delta", delta);
	shader->setUniform("u_irr_size", size);
	shader->setUniform("u_irr_normal_dist", normal_dist);
	int probes_num = probes.size();
	shader->setUniform("u_irr_probes_num", probes_num);
	shader->setUniform("u_irr_dimensions", dimensions);
	shader->setUniform("u_irr_active", active);
	if(probes_texture != NULL)
		shader->setUniform("u_probes_texture", probes_texture, 9);
}

void GTR::IrradianceEntity::save(){
	// saveIrradianceToDisk ---------------------------------
	//fill header structure
	sIrrHeader header;

	header.start = start_pos;
	header.end = end_pos;
	header.dims = dimensions;
	header.delta = delta;
	header.num_probes = probes.size();

	//write to file header and probes data
	FILE* f = fopen("data/irradianceData/irradiance.bin", "wb");
	fwrite(&header, sizeof(header), 1, f);
	fwrite(&(probes[0]), sizeof(sProbe), probes.size(), f);
	fclose(f);

}

bool GTR::IrradianceEntity::read(const char* filename){
	//load probes info from disk
	FILE* f = fopen(filename, "rb");
	if (!f)
		return false;

	//read header
	sIrrHeader header;
	fread(&header, sizeof(header), 1, f);

	//copy info from header to our local vars
	start_pos = header.start;
	end_pos = header.end;
	dimensions = header.dims;
	delta = header.delta;
	int num_probes = header.num_probes;

	//allocate space for the probes
	probes.resize(num_probes);

	//read from disk directly to our probes container in memory
	fread(&probes[0], sizeof(sProbe), probes.size(), f);
	fclose(f);

	//build the texture again…
	probesToTexture();
	std::cout << "+ Irradiance uploaded" << std::endl;
	return true;
}

/************************************************************************************************************/
GTR::reflectionProbeEntity::reflectionProbeEntity(){
	cubemap = new Texture();
	cubemap->createCubemap(
		512, 512,
		NULL,
		GL_RGB, GL_UNSIGNED_INT, false);
}

void GTR::reflectionProbeEntity::configure(cJSON* json){
	Scene::instance->reflectionProbes.push_back(this);
}
