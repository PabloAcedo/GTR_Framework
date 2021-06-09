#include "renderer.h"

#include "camera.h"
#include "shader.h"
#include "mesh.h"
#include "texture.h"
#include "prefab.h"
#include "material.h"
#include "utils.h"
#include "scene.h"
#include "fbo.h"
#include "application.h"
#include "extra/hdre.h"
#include <algorithm>


using namespace GTR;

//temporal probe test
sProbe probe;

void updateFBO(FBO& fbo, int textures_num, bool quality) {

	float scale = 1.0;
	int w = Application::instance->window_width * scale;
	int h = Application::instance->window_height * scale;
	if (fbo.fbo_id == 0 || (w != fbo.width) || (h != fbo.height)) {//Initialize fbo
		fbo.~FBO();
		if (quality) {
			fbo.create(w,
				h,
				textures_num, GL_RGBA, GL_FLOAT, true);
		}
		else {
			fbo.create(w,
				h,
				textures_num, GL_RGBA, GL_UNSIGNED_BYTE, true);
		}

	}
}

RenderCall::RenderCall(Matrix44 model, Mesh* mesh, Material* material) {
	this->model = model;
	this->mesh = mesh;
	this->material = material;
	distance2Cam = 0.0;
}

bool sortByAlpha(RenderCall* i, RenderCall* j) {
	return(i->material->alpha_mode < j->material->alpha_mode);
}

bool sortByDistance(RenderCall* i, RenderCall* j) {
	return(i->distance2Cam > j->distance2Cam);
}


Renderer::Renderer() {
	current_mode =  1;
	current_mode_pipeline = current_mode_ilum = 1;
	renderingShadows = false;
	render_mode = GTR::eRenderMode::LIGHT_MULTI;
	pipeline_mode = GTR::ePipelineMode::DEFERRED;
	showGbuffers = false;
	cast_shadows = true;
	ilum_mode = GTR::eIlumMode::PBR;
	ao_map = NULL;
	showSSAO = false;
	avg_lum = 0.78;
	lum_white = 1.0;
	gamma = 2.2;
	scale_tonemap = 1.0;
	apply_tonemap = true;
	apply_ssao = false;


	//temporal probe test
	memset(&probe, 0, sizeof(probe));
	probe.pos.set(76, 38, 96);
	probe.sh.coeffs[0].set(1, 0, 0);

	irr_fbo = NULL;

}

void Renderer::collectRenderCalls(GTR::Scene* scene, Camera* camera)
{
	renderCalls.clear();
	renderCalls_Blending.clear();
	//render entities
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//is a prefab!
		if (ent->entity_type == PREFAB)
		{
			PrefabEntity* pent = (GTR::PrefabEntity*)ent;
			if (pent->prefab)
				renderPrefab(ent->model, pent->prefab, camera);
		}
	}

	//sort the rendercalls
	if (camera) {
		std::sort(renderCalls.begin(), renderCalls.end(), sortByDistance);
		std::sort(renderCalls_Blending.begin(), renderCalls_Blending.end(), sortByDistance);
	}
	
}

void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{
	collectRenderCalls(scene, camera);

	if (pipeline_mode == FORWARD || renderingShadows) {
		if (!renderingShadows) {
			updateFBO(scene_fbo, 1,true);
			scene_fbo.bind();
		}
		//set the clear color (the background color)
		glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
		// Clear the color and the depth buffer
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		checkGLErrors();
		renderForward(scene, renderCalls, camera);
		if (!renderingShadows) {
			renderForward(scene, renderCalls_Blending, camera);
		}
		if (!renderingShadows) {
			scene_fbo.unbind();
			renderFinal();
		}
		
	}
	else if (pipeline_mode == DEFERRED) { 
		//renderCalls.insert(renderCalls.end(), renderCalls_Blending.begin(), renderCalls_Blending.end());
		renderDeferred(scene, renderCalls, camera);
	}
}

void Renderer::renderForward(Scene* scene, std::vector<RenderCall*>& rc, Camera* camera) {
	//render
	for (int i = 0; i < rc.size(); i++) {
		renderMeshWithMaterial(render_mode,rc[i]->model, rc[i]->mesh, rc[i]->material, camera);
	}
	glDisable(GL_BLEND);
	glDepthFunc(GL_LESS);
}


//renders all the prefab
void Renderer::renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera)
{
	assert(prefab && "PREFAB IS NULL");
	//assign the model to the root node
	renderNode(model, &prefab->root, camera);
}

//renders a node of the prefab and its children
void Renderer::renderNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
{
	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);
		
		//if bounding box is inside the camera frustum then the object is probably visible
		if (!camera || camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )	//lazy evaluation, si es compleix !camera, entra
		{
			
			
			RenderCall* rc = new RenderCall(node_model, node->mesh, node->material);
			
			if(camera)	//Agafar nomes els rc que veu la camera... COMPROVAR
				rc->distance2Cam = world_bounding.center.distance(camera->eye);
			
			if (node->material->alpha_mode == BLEND && !renderingShadows) {
				renderCalls_Blending.push_back(rc);
			}
			else {
				renderCalls.push_back(rc);
			}
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		renderNode(prefab_model, node->children[i], camera);
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(eRenderMode mode, const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;

	//select the blending
	if (material->alpha_mode == GTR::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if(material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);
	if (!renderingShadows) {
		if (mode == GTR::eRenderMode::TEXTURE) {
			shader = Shader::Get("texture");
			if (shader == NULL)
				return;
			shader->enable();
			commonUniforms(shader, model, material, camera, mesh, false);
			//disable shader
			shader->disable();
		}
		else if (mode == GTR::eRenderMode::LIGHT_MULTI) {
			shader = Shader::Get("multi_pass");
			if (shader == NULL)
				return;
			shader->enable();
			multipassRendering(shader, model, material, camera, mesh);
			//disable shader
			shader->disable();
		}
		else if (mode == GTR::eRenderMode::LIGHT_SINGLE) {
			shader = Shader::Get("single_pass");
			if (shader == NULL)
				return;
			shader->enable();
			singlepassUniforms(shader, model, material, camera, mesh);
			shader->disable();
		}else if (mode == GTR::eRenderMode::GBUFFERS) {
			shader = Shader::Get("g_buffers");
			if (shader == NULL)
				return;
			shader->enable();
			Texture* texture = material->metallic_roughness_texture.texture;
			uploadExtraMap(shader, texture, "u_omr", "u_has_omr", 1);
			texture = material->emissive_texture.texture;
			uploadExtraMap(shader, texture, "u_emissive", "u_has_emissive", 2);
			texture = material->normal_texture.texture;
			uploadExtraMap(shader, texture, "u_normal_map", "u_has_normal", 3);
			commonUniforms(shader, model, material, camera, mesh, false);
			shader->disable();
		}
	}
	else {
		if (material->alpha_mode == GTR::eAlphaMode::BLEND) return;
		if (material->alpha_mode == GTR::eAlphaMode::MASK) {
			shader = Shader::Get("texture");
		}
		else {
			shader = Shader::Get("flat");
		}
		if (shader == NULL) return;
		shader->enable();
		commonUniforms(shader, model, material, camera, mesh, false);
		shader->disable();
	}

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glDepthFunc(GL_LESS);
}

void Renderer::commonUniforms(Shader*& shader, const Matrix44 model, GTR::Material* material, Camera* camera, Mesh* mesh, bool fromOther) {
	Texture* texture = NULL;
	//upload uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	float t = getTime();
	shader->setUniform("u_time", t);

	shader->setUniform("u_color", material->color);
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	texture = material->color_texture.texture;
	if (texture)
		shader->setUniform("u_albedo", texture, 0);

	if (!fromOther)
		mesh->render(GL_TRIANGLES);
}

void Renderer::uploadExtraMap(Shader*& shader, Texture* texture, const char* uniform_name, const char* bool_name, int tex_slot) {
	if (texture) {
		shader->setUniform(bool_name, true);
		shader->setUniform(uniform_name, texture, tex_slot);
	}
	else {
		if (pipeline_mode == GTR::ePipelineMode::DEFERRED) {
			texture = Texture::getWhiteTexture();
			if (uniform_name == "u_emissive")
				texture = Texture::getBlackTexture();

			shader->setUniform(uniform_name, texture, tex_slot);
		}
		shader->setUniform(bool_name, false);
	}
	
}

void Renderer::multipassUniforms(GTR::LightEntity* light, Shader*& shader, const Matrix44 model, GTR::Material* material, Camera* camera, Mesh* mesh, int iteration) {

	light->lightVisible();

	Texture* texture = NULL;

	commonUniforms(shader, model, material, camera, mesh, true);//upload common uniforms

	texture = material->emissive_texture.texture;
	uploadExtraMap(shader, texture, "u_emissive", "u_has_emissive", 1);
	shader->setUniform("u_iteration", iteration);

	texture = material->normal_texture.texture;
	uploadExtraMap(shader, texture, "u_normal_map", "u_has_normal", 2);

	texture = material->metallic_roughness_texture.texture;
	uploadExtraMap(shader, texture, "u_omr", "u_has_omr", 3);
	shader->setUniform("u_ilum_mode", ilum_mode);
	
	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	//upload lights
	if (light != NULL)
		light->uploadUniforms(shader);

	if (Scene::instance != NULL)
		shader->setUniform("u_light_ambient", Scene::instance->ambient_light);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);
}

void Renderer::multipassRendering(Shader*& shader, const Matrix44 model, GTR::Material* material, Camera* camera, Mesh* mesh) {

	glDepthFunc(GL_LEQUAL);

	//multipass
	for (int i = 0; i < Scene::instance->lights.size(); i++) {

		if (i == 0 && material->alpha_mode != GTR::eAlphaMode::BLEND) {
			glDisable(GL_BLEND);
		}
		else {
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			glDepthFunc(GL_LEQUAL);
		}
		if (i == 0 && material->alpha_mode == GTR::eAlphaMode::BLEND) {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
		multipassUniforms(Scene::instance->lights[i], shader, model, material, camera, mesh, i);
	}
	glDisable(GL_BLEND);
}

void Renderer::singlepassUniforms(Shader*& shader, const Matrix44 model, GTR::Material* material, Camera* camera, Mesh* mesh) {
	Texture* texture = NULL;

	commonUniforms(shader, model, material, camera, mesh, true);//upload common uniforms

	texture = material->emissive_texture.texture;
	uploadExtraMap(shader, texture, "u_emissive_color", "u_has_emissive", 1);

	texture = material->normal_texture.texture;
	uploadExtraMap(shader, texture, "u_normal_map", "u_has_normal", 2);

	texture = material->metallic_roughness_texture.texture;
	uploadExtraMap(shader, texture, "u_omr", "u_has_ao", 3);

	if (Scene::instance != NULL) {

		Scene::instance->updateLights();

		int num_lights = Scene::instance->lights.size();
		shader->setUniform("u_light_ambient", Scene::instance->ambient_light);
		shader->setUniform3Array("u_light_pos", (float*)Scene::instance->light_pos, num_lights);
		shader->setUniform3Array("u_light_color", (float*)Scene::instance->light_color, num_lights);
		shader->setUniform1("u_num_lights", num_lights);
		shader->setUniform3Array("u_light_direction", (float*)Scene::instance->light_direction, num_lights);
		shader->setUniform1Array("u_light_type", Scene::instance->light_type, num_lights);
		shader->setUniform1Array("u_light_maxdist", (float*)Scene::instance->l_max_dist, num_lights);
		shader->setUniform1Array("u_cosCutoff", (float*)Scene::instance->l_cone_angle, num_lights);
		shader->setUniform1Array("u_light_intensity", (float*)Scene::instance->l_intensity, num_lights);
		shader->setUniform1Array("u_spot_exp", (float*)Scene::instance->l_spotExp, num_lights);
	}

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

}

void Renderer::renderShadowMaps(Scene* scene) {
	if (!cast_shadows) return;

	renderingShadows = true;
	for (int i = 0; i < scene->lights.size(); i++) {
		if (scene->lights[i]->light_type == POINT || !scene->lights[i]->visible)
			continue;

		if (scene->lights[i]->fbo == NULL) {
			scene->lights[i]->fbo = new FBO();
			scene->lights[i]->fbo->setDepthOnly(1024, 1024);
		}

		scene->lights[i]->fbo->bind();
		glColorMask(false, false, false, false);
		glClear(GL_DEPTH_BUFFER_BIT);
		Matrix44 model = scene->lights[i]->model;
		scene->lights[i]->orientCam();
		scene->lights[i]->cam->enable();
		renderScene(scene, scene->lights[i]->cam);
		scene->lights[i]->fbo->unbind();
		glColorMask(true, true, true, true);

	}
	renderingShadows = false;
}

void Renderer::displayShadowMap() {
	for (int i = 0; i < Scene::instance->lights.size(); i++) {
		LightEntity* light = Scene::instance->lights[i];
		if (light->show_shadowmap) {
			Shader* shader = Shader::Get("depth");
			shader->enable();
			shader->setUniform("u_camera_nearfar", Vector2(light->cam->near_plane, light->cam->far_plane));
			int w = Application::instance->window_width;
			int h = Application::instance->window_height;
			glViewport(10, 10, 300, 300);
			if (light->light_type == SPOT)
				light->fbo->depth_texture->toViewport(shader);
			else
				light->fbo->depth_texture->toViewport();
			shader->disable();
			glViewport(0, 0, w, h);
		}
	}

}

void Renderer::changeRenderMode() {
	if (current_mode == 0) {
		render_mode = GTR::eRenderMode::TEXTURE;
	}
	else if (current_mode == 1) {
		render_mode = GTR::eRenderMode::LIGHT_MULTI;
	}
	else if (current_mode == 2) {
		render_mode = GTR::eRenderMode::LIGHT_SINGLE;
	}
	changePipelineMode();
	changeIlumMode();
}

void Renderer::changePipelineMode() {
	if (current_mode_pipeline == 0) {
		pipeline_mode = GTR::ePipelineMode::FORWARD;
	}
	else if (current_mode_pipeline == 1) {
		pipeline_mode = GTR::ePipelineMode::DEFERRED;
	}

}

void Renderer::changeIlumMode() {
	if (current_mode_ilum == 0) {
		ilum_mode = GTR::eIlumMode::PHONG;
		Scene::instance->phong = true;
	}
	else if (current_mode_ilum == 1) {
		ilum_mode = GTR::eIlumMode::PBR;
		Scene::instance->phong = false;
	}

}

/********************************************************************************************************************/
//deferred
void Renderer::renderDeferred(Scene* scene, std::vector<RenderCall*>& rc, Camera* camera) {

	glDisable(GL_BLEND);

	updateFBO(fbo_gbuffers, 4, false);
	int w = fbo_gbuffers.width; int h = fbo_gbuffers.height;

	//render gbuffers
	fbo_gbuffers.bind();

	glClearColor(0, 0, 0, 0);
	//glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	for (int i = 0; i < rc.size(); i++) {
		renderMeshWithMaterial(GTR::eRenderMode::GBUFFERS,rc[i]->model, rc[i]->mesh, rc[i]->material, camera);
	}

	fbo_gbuffers.unbind();

	//ssao+
	if (ao_map == NULL || ao_map->width != w || ao_map->height != h) {
		ao_map = new Texture(w, h, GL_RGB, GL_UNSIGNED_BYTE);
	}

	//bind the texture we want to change
	fbo_gbuffers.depth_texture->bind();

	//enable bilinear filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	fbo_gbuffers.depth_texture->unbind();

	if(apply_ssao)
		ssao.compute(fbo_gbuffers.depth_texture, fbo_gbuffers.color_textures[1], camera, ao_map);

	updateFBO(scene_fbo, 1, true);

	//fbo_gbuffers.depth_texture->copyTo(scene_fbo.depth_texture);

	scene_fbo.bind();
	//Render deferred
	glClearColor(0, 0, 0, 0);

	fbo_gbuffers.depth_texture->copyTo(NULL);

	//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClear(GL_COLOR_BUFFER_BIT);
	checkGLErrors();

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);

	multipassDeferred(camera);

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);

	//forward pass for blending objects
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	renderForward(scene, renderCalls_Blending, camera);

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);

	//temporal test probes
	if (scene->irradianceEnt == NULL) {
		scene->irradianceEnt = new IrradianceEntity();
		scene->irradianceEnt->placeProbes();
	} 

	//temporal test probes
	for (int i = 0; i < scene->irradianceEnt->probes.size(); i++) {
		sProbe probe2 = scene->irradianceEnt->probes[i];
		renderProbe(probe2.pos, 3.0, probe2.sh.coeffs[0].v);
	}
	//scene->irradianceEnt->probes_texture->bind();
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);


	scene_fbo.unbind();

	renderFinal();

	if(showGbuffers)
		showgbuffers(camera);

	if (showSSAO) {
		ao_map->toViewport();
	}
		
}

void GTR::Renderer::showgbuffers(Camera* camera) {
	
	int width = Application::instance->window_width;
	int height = Application::instance->window_height;

	glViewport(0, height*0.5, width * 0.5, height * 0.5);
	fbo_gbuffers.color_textures[0]->toViewport();

	glViewport(width * 0.5, height*0.5, width * 0.5, height * 0.5);
	fbo_gbuffers.color_textures[1]->toViewport();

	glViewport(0, 0, width * 0.5, height * 0.5);
	fbo_gbuffers.color_textures[2]->toViewport();

	glViewport(width * 0.5, 0, width * 0.5, height * 0.5);
	Shader* shader = Shader::Get("depth");
	shader->enable();
	shader->setUniform("u_camera_nearfar", Vector2(camera->near_plane, camera->far_plane));
	fbo_gbuffers.depth_texture->toViewport(shader);
	shader->disable();

	glViewport(0, 0, width, height);
}

void GTR::Renderer::multipassUniformsDeferred(LightEntity* light, Camera* camera, int iteration) {
	if (!light->visible) return;

	Mesh* mesh = NULL;
	Shader* shader = NULL;

	if (light->light_type == DIRECTIONAL) {
		shader = Shader::Get("deferred_multi_pass");
		mesh = Mesh::getQuad();
	}
	else {
		shader = Shader::Get("deferred_geometry");
		mesh = Mesh::Get("data/meshes/sphere.obj", false);
		glEnable(GL_CULL_FACE);
	}
	
	if (shader == NULL) return;

	shader->enable();
	//pass the gbuffers to the shader
	shader->setUniform("u_albedo", fbo_gbuffers.color_textures[0], 0);
	shader->setUniform("u_normal_texture", fbo_gbuffers.color_textures[1], 1);
	shader->setUniform("u_omr", fbo_gbuffers.color_textures[2], 2);
	shader->setUniform("u_depth_texture", fbo_gbuffers.depth_texture, 3);
	shader->setUniform("u_emissive", fbo_gbuffers.color_textures[3], 4);
	shader->setUniform("u_ilum_mode", ilum_mode);
	shader->setUniform("u_has_omr", true);
	shader->setUniform("u_ssao", ao_map, 5);//apply_ssao
	shader->setUniform("u_apply_ssao", apply_ssao);

	Matrix44 vp_inv = camera->viewprojection_matrix;
	vp_inv.inverse();
	//pass the inverse projection of the camera to reconstruct world pos.
	shader->setUniform("u_inverse_viewprojection", vp_inv);

	shader->setUniform("u_camera_position", camera->eye);

	//pass the inverse window resolution, this may be useful
	int width = Application::instance->window_width;
	int height = Application::instance->window_height;
	shader->setUniform("u_iRes", Vector2(1.0 / (float)width, 1.0 / (float)height));

	light->uploadUniforms(shader);

	Matrix44 m;
	Vector3 pos = light->model.getTranslation();
	m.setTranslation(pos.x, pos.y, pos.z);
	m.scale(light->max_dist, light->max_dist, light->max_dist);
	shader->setUniform("u_model", m);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	if(light->light_type != DIRECTIONAL) glFrontFace(GL_CW);

	//render a fullscreen quad or sphere
	mesh->render(GL_TRIANGLES);

	shader->disable();

	glFrontFace(GL_CCW);
}

void GTR::Renderer::multipassDeferred(Camera* camera) {
	renderAmbient();
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	for (int i = 0; i < Scene::instance->lights.size(); i++) {
		LightEntity* light = Scene::instance->lights[i];
		multipassUniformsDeferred(light, camera, i);		
	}
}

void GTR::Renderer::renderAmbient(){
	Shader* shader = Shader::Get("deferred_ambient");
	if (shader == NULL) return;
	Mesh* quad = Mesh::getQuad();
	shader->enable();
	shader->setUniform("u_albedo", fbo_gbuffers.color_textures[0], 0);
	shader->setUniform("u_normal_texture", fbo_gbuffers.color_textures[1], 1);
	shader->setUniform("u_omr", fbo_gbuffers.color_textures[2], 2);
	shader->setUniform("u_depth_texture", fbo_gbuffers.depth_texture, 3);
	shader->setUniform("u_emissive", fbo_gbuffers.color_textures[3], 4);
	shader->setUniform("u_has_omr", true);
	shader->setUniform("u_ssao", ao_map, 5);//apply_ssao
	shader->setUniform("u_apply_ssao", apply_ssao);
	//pass the inverse window resolution, this may be useful
	int width = Application::instance->window_width;
	int height = Application::instance->window_height;
	shader->setUniform("u_iRes", Vector2(1.0 / (float)width, 1.0 / (float)height));
	shader->setUniform("u_light_ambient", Scene::instance->ambient_light);
	quad->render(GL_TRIANGLES);
	shader->disable();
}

/********************************************************************************************************************/
void GTR::Renderer::renderInMenu(){
	ImGui::Combo("Pipeline Mode", &current_mode_pipeline, optionsTextPipeline, IM_ARRAYSIZE(optionsTextPipeline));
	if (current_mode_pipeline != GTR::ePipelineMode::DEFERRED) {
		ImGui::Combo("Render Mode", &current_mode, optionsText, IM_ARRAYSIZE(optionsText));
	}
	ImGui::Combo("Ilumination Mode", &current_mode_ilum, optionsTextIlum, IM_ARRAYSIZE(optionsTextIlum));
	changeRenderMode();
	ImGui::Checkbox("Update Shadows", &cast_shadows);

	if (current_mode_pipeline == GTR::ePipelineMode::DEFERRED) {
		ImGui::Checkbox("Show gbuffers", &showGbuffers);
		if(ImGui::TreeNode("SSAO")) {
			ImGui::Checkbox("Apply SSAO", &apply_ssao);
			if (apply_ssao) {
				ImGui::SliderFloat("bias_ao", &ssao.bias_slider, 0.001, 0.6);
				ImGui::SliderFloat("radius_ao", &ssao.radius_slider, 1.0, 30.0);
				ImGui::SliderFloat("distance_ao", &ssao.max_distance_slider, 0.01, 1.0);
				ImGui::Checkbox("Show SSAO map", &showSSAO);
			}
			else {
				showSSAO = false;
			}
		}
	}
	if (ImGui::TreeNode("Tonemapper")) {
		ImGui::Checkbox("Apply tonemap", &apply_tonemap);
		if (apply_tonemap) {
			ImGui::SliderFloat("Average luminance", &avg_lum, 0.5, 4.0);
			ImGui::SliderFloat("White luminance", &lum_white, 0.5, 1.0);
			ImGui::SliderFloat("Scale tonemap", &scale_tonemap, 0.001, 5.0);
		}
		
	}
}

void GTR::Renderer::renderFinal(){
	Shader* shader = Shader::Get("tonemapper");
	shader->enable();
	shader->setUniform("u_average_lum", avg_lum);
	shader->setUniform("u_lumwhite2", lum_white * lum_white);
	shader->setUniform("u_scale", scale_tonemap); 
	shader->setUniform("u_apply", apply_tonemap);
	scene_fbo.color_textures[0]->toViewport(shader);
	shader->disable();
}


//Irradiance & probes

void GTR::Renderer::renderProbe(Vector3 pos, float size, float* coeffs)
{

	//Com passar dades a renderProbe: (Vector3(),size, probe.sh.coeffs[0].v)


	Camera* camera = Camera::current;
	Shader* shader = Shader::Get("probe");
	Mesh* mesh = Mesh::Get("data/meshes/sphere.obj", false);

	glEnable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	Matrix44 model;
	model.setTranslation(pos.x, pos.y, pos.z);
	model.scale(size, size, size);

	shader->enable();
	shader->setUniform("u_viewprojection",
		camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform3Array("u_coeffs", coeffs, 9);

	mesh->render(GL_TRIANGLES);
}


void GTR::Renderer::computeProbe(Scene* scene,  sProbe& p){
	FloatImage images[6]; //here we will store the six views
	Camera cam;

	//set the fov to 90 and the aspect to 1
	cam.setPerspective(90, 1, 0.1, 1000);

	
	if (!irr_fbo) {
		
		irr_fbo = new FBO();
		irr_fbo->create(64, 64,1, GL_RGB, GL_FLOAT); //Tamany pot canviar, no ha de ser molt gran

	}		


	collectRenderCalls(scene, NULL);	//agafara tots els rc, independentment del que vegi la camera (pq ho volem???)

	for (int i = 0; i < 6; ++i) //for every cubemap face
	{
		//compute camera orientation using defined vectors
		Vector3 eye = p.pos;
		Vector3 front = cubemapFaceNormals[i][2];
		Vector3 center = p.pos + front;
		Vector3 up = cubemapFaceNormals[i][1];
		cam.lookAt(eye, center, up);
		cam.enable();

		//render the scene from this point of view
		irr_fbo->bind();
		renderForward(scene, renderCalls, &cam);
		irr_fbo->unbind();

		//read the pixels back and store in a FloatImage
		images[i].fromTexture(irr_fbo->color_textures[0]);
	}

	//compute the coefficients given the six images
	p.sh = computeSH(images,false);	//falta gamma correcte? 1.0 arbitrari
}

void GTR::Renderer::computeProbes(Scene* scene) {
	for (int i = 0; i < scene->irradianceEnt->probes.size(); i++) {
		computeProbe(scene, scene->irradianceEnt->probes[i]);
	}
	scene->irradianceEnt->probesToTexture();
}

void GTR::Renderer::updateIrradianceCache(GTR::Scene* scene) {	//actualitza les probes

	//probe.pos = Vector3(0, 1, 0); 
	computeProbes(scene);

}


/********************************************************************************************************************/
std::vector<Vector3> generateSpherePoints(int num, float radius, bool hemi)
{
	std::vector<Vector3> points;
	points.resize(num);
	for (int i = 0; i < num; i += 3)
	{
		Vector3& p = points[i];
		float u = random();
		float v = random();
		float theta = u * 2.0 * PI;
		float phi = acos(2.0 * v - 1.0);
		float r = cbrt(random() * 0.9 + 0.1) * radius;
		float sinTheta = sin(theta);
		float cosTheta = cos(theta);
		float sinPhi = sin(phi);
		float cosPhi = cos(phi);
		p.x = r * sinPhi * cosTheta;
		p.y = r * sinPhi * sinTheta;
		p.z = r * cosPhi;
		if (hemi && p.z < 0)
			p.z *= -1.0;
	}
	return points;
}

GTR::SSAOFX::SSAOFX(){
	points = generateSpherePoints(64, 1.0, true);
	bias_slider = 0.015;
	radius_slider = 10.0f;
	max_distance_slider = 0.13f;
}

void GTR::SSAOFX::compute(Texture* depth_buffer, Texture* normal_buffer, Camera* camera, Texture* output){
	//FBO* fbo = Texture::getGlobalFBO(output);
	updateFBO(ao_fbo, 1, false);
	int w = ao_fbo.width; int h = ao_fbo.height;
	/*if (ao_fbo.fbo_id == 0 || (w != Application::instance->window_width) || (h != Application::instance->window_height)) {//Initialize fbo
		ao_fbo.create(Application::instance->window_width,
			Application::instance->window_height,
			1, GL_RGBA, GL_UNSIGNED_BYTE);
	}*/
	ao_fbo.setTexture(output);
	ao_fbo.bind();
	
	Shader* shader = Shader::Get("ssao");
	if (shader == NULL) 
		return;

	Mesh* quad = Mesh::getQuad();

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	shader->enable();
	shader->setUniform("u_depth_texture", depth_buffer, 3);
	shader->setUniform("u_normal_texture", normal_buffer, 1);

	shader->setUniform("u_camera_pos", camera->eye);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	Matrix44 inv_vp = camera->viewprojection_matrix;
	inv_vp.inverse();
	shader->setUniform("u_inverse_viewprojection", inv_vp);

	shader->setUniform3Array("u_points", points[0].v, points.size());


	shader->setUniform("u_bias_slider", bias_slider);
	shader->setUniform("u_radius", radius_slider);
	shader->setUniform("u_max_distance", max_distance_slider);

	//pass the inverse window resolution, this may be useful
	int width = Application::instance->window_width;
	int height = Application::instance->window_height;
	shader->setUniform("u_iRes", Vector2(1.0 / (float)width, 1.0 / (float)height));

	quad->render(GL_TRIANGLES);
	shader->disable();

	ao_fbo.unbind();
}

/********************************************************************************************************************/
Texture* GTR::CubemapFromHDRE(const char* filename)
{
	HDRE* hdre = HDRE::Get(filename);
	if (!hdre)
		return NULL;

	Texture* texture = new Texture();
	if (hdre->getFacesf(0))
	{
		texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesf(0),
			hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT);
		for (int i = 1; i < hdre->levels; ++i)
			texture->uploadCubemap(texture->format, texture->type, false,
				(Uint8**)hdre->getFacesf(i), GL_RGBA32F, i);
	}
	else
		if (hdre->getFacesh(0))
		{
			texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesh(0),
				hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_HALF_FLOAT);
			for (int i = 1; i < hdre->levels; ++i)
				texture->uploadCubemap(texture->format, texture->type, false,
					(Uint8**)hdre->getFacesh(i), GL_RGBA16F, i);
		}
	return texture;
}