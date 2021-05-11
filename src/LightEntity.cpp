/*
#include "LightEntity.h"
#include "shader.h"
#include "extra/cJSON.h"
#include "utils.h"
#include "scene.h"
#include "camera.h"
#include "fbo.h"
#include "texture.h"
#include "application.h"

using namespace GTR;

LightEntity::LightEntity(){
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

LightEntity::~LightEntity() {

}

void LightEntity::setColor(vec3 color) {
	this->color = color;
}

void LightEntity::renderInMenu() {
	BaseEntity::renderInMenu();
#ifndef SKIP_IMGUI
	if (visible) {
		ImGui::ColorEdit3("Light Color", color.v);
		ImGui::SliderFloat("Max distance", &max_dist, 1.0, 5000.0);
		ImGui::SliderFloat("Intensity", &intensity, 1.0, 10.0);

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

void LightEntity::configure(cJSON* json) {

	if (cJSON_GetObjectItem(json, "light_color")){
		this->color = readJSONVector3(json, "light_color", Vector3());
	}
	if (cJSON_GetObjectItem(json, "light_type")){
		std::string t = cJSON_GetObjectItem(json, "light_type")->valuestring;
		if (t == "directional" || t == "DIRECTIONAL")
			light_type = GTR::eLightType::DIRECTIONAL;
		else if(t == "point" || t == "POINT")
			light_type = GTR::eLightType::POINT;
		else if (t == "spot" || t == "SPOT")
			light_type = GTR::eLightType::SPOT;
	}
	if (cJSON_GetObjectItem(json, "max_dist")){
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
		model.setFrontAndOrthonormalize(target-model.getTranslation());
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
		fbo->setDepthOnly(1024*2, 1024*2);
		if (light_type == GTR::eLightType::SPOT) {
			cam->lookAt(model.getTranslation(), model.getTranslation() + model.frontVector(), Vector3(0, 1, 0));
			cam->setPerspective(cone_angle * 2.0, 1.0, 0.1f, 3000.f);
		}
		else {
			cam->lookAt(model.getTranslation(),model.frontVector(), model.topVector());
			cam->setOrthographic(-area_size,area_size,-area_size,area_size, 0.1f, 3000.f);
			this->bias = 0.01;
		}
		
	}

}

void LightEntity::uploadUniforms(Shader* &shader) {
	shader->setUniform("u_light_color", this->color);
	shader->setUniform("u_light_pos", this->model.getTranslation());
	shader->setUniform("u_light_type", light_type);
	shader->setUniform("u_light_maxdist", max_dist);
	shader->setUniform("u_light_direction", model.frontVector());
	shader->setUniform("u_cosCutoff", (float)cos(cone_angle*DEG2RAD));
	shader->setUniform("u_light_intensity", intensity);
	shader->setUniform("u_spot_exp", spotExp);
	shader->setUniform("u_shadow_bias", this->bias);
}

void LightEntity::lightVisible() {
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

void LightEntity::orientCam() {
	if (light_type == GTR::eLightType::SPOT) {
		cam->setPerspective(cone_angle * 2.0, 1.0, 0.1f, 3000.f);
		cam->lookAt(model.getTranslation(), model.getTranslation() + model.frontVector(), Vector3(0, 1, 0));
	}
	else if (light_type == GTR::eLightType::DIRECTIONAL) {
		//Vector3 pos = Scene::instance->mainCam->eye;
		cam->lookAt(model.getTranslation(), model.getTranslation() + model.frontVector(), Vector3(0, 1, 0)); 
	}
}

*/