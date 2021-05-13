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
	current_mode_pipeline = 0;
	renderingShadows = false;
	render_mode = GTR::eRenderMode::LIGHT_MULTI;
	pipeline_mode = GTR::ePipelineMode::FORWARD;
	showGbuffers = false;
	cast_shadows = true;
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
	std::sort(renderCalls.begin(), renderCalls.end(), sortByDistance);
}

void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{
	collectRenderCalls(scene, camera);

	if (pipeline_mode == FORWARD || renderingShadows) {
		//set the clear color (the background color)
		glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
		// Clear the color and the depth buffer
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		checkGLErrors();

		renderForward(scene, renderCalls, camera);
		if (!renderingShadows) {
			std::sort(renderCalls_Blending.begin(), renderCalls_Blending.end(), sortByDistance);
			renderForward(scene, renderCalls_Blending, camera);
		}
	}
	else if (pipeline_mode == DEFERRED)
		renderDeferred(scene, renderCalls, camera);

	/*if (!renderingShadows) {
		std::sort(renderCalls_Blending.begin(), renderCalls_Blending.end(), sortByDistance);
		renderForward(scene, renderCalls_Blending, camera);
	}*/
}

void Renderer::renderForward(Scene* scene, std::vector<RenderCall*>& rc, Camera* camera) {
	//render
	for (int i = 0; i < rc.size(); i++) {
		renderMeshWithMaterial(render_mode,rc[i]->model, rc[i]->mesh, rc[i]->material, camera);
	}
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
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
		{
			RenderCall* rc = new RenderCall(node_model, node->mesh, node->material);
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
		shader->setUniform("u_texture", texture, 0);

	if (!fromOther)
		mesh->render(GL_TRIANGLES);
}

void Renderer::uploadExtraMap(Shader*& shader, Texture* texture, const char* uniform_name, const char* bool_name, int tex_slot) {
	if (texture) {
		shader->setUniform(bool_name, true);
	}
	else {
		texture = Texture::getWhiteTexture(); //a 1x1 white texture
		shader->setUniform(bool_name, false);
	}
	shader->setUniform(uniform_name, texture, tex_slot);
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
	uploadExtraMap(shader, texture, "u_omr", "u_has_ao", 3);

	if (light->light_type != POINT && cast_shadows) {
		//get the depth texture from the FBO
		Texture* shadowmap = light->fbo->depth_texture;

		//pass it to the shader in slot 8
		shader->setTexture("u_shadowmap", shadowmap, 8);

		//also get the viewprojection from the light
		Matrix44 shadow_proj = light->cam->viewprojection_matrix;

		//pass it to the shader
		shader->setUniform("u_shadow_viewproj", shadow_proj);
	}


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
		}
		if (i == 0 && material->alpha_mode == GTR::eAlphaMode::BLEND) {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
		multipassUniforms(Scene::instance->lights[i], shader, model, material, camera, mesh, i);
	}
}

void Renderer::singlepassUniforms(Shader*& shader, const Matrix44 model, GTR::Material* material, Camera* camera, Mesh* mesh) {
	Texture* texture = NULL;

	commonUniforms(shader, model, material, camera, mesh, true);//upload common uniforms

	texture = material->emissive_texture.texture;
	uploadExtraMap(shader, texture, "u_emissive", "u_has_emissive", 1);

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

}

void Renderer::changePipelineMode() {
	if (current_mode_pipeline == 0) {
		pipeline_mode = GTR::ePipelineMode::FORWARD;
	}
	else if (current_mode_pipeline == 1) {
		pipeline_mode = GTR::ePipelineMode::DEFERRED;
	}

}

/****************************************************************************************/
//deferred
void Renderer::renderDeferred(Scene* scene, std::vector<RenderCall*>& rc, Camera* camera) {

	glDisable(GL_BLEND);

	if (fbo_gbuffers.fbo_id == 0) {
		fbo_gbuffers.create(Application::instance->window_width,
			Application::instance->window_height,
			3, GL_RGBA, GL_UNSIGNED_BYTE);
	}

	fbo_gbuffers.bind();

	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	for (int i = 0; i < rc.size(); i++) {
		renderMeshWithMaterial(GTR::eRenderMode::GBUFFERS,rc[i]->model, rc[i]->mesh, rc[i]->material, camera);
	}

	fbo_gbuffers.unbind();
	if(showGbuffers)
		showgbuffers(camera);
}

void GTR::Renderer::showgbuffers(Camera* camera) {
	glDisable(GL_BLEND);

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





Texture* GTR::CubemapFromHDRE(const char* filename)
{
	HDRE* hdre = new HDRE();
	if (!hdre->load(filename))
	{
		delete hdre;
		return NULL;
	}

	/*
	Texture* texture = new Texture();
	texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFaces(0), hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT );
	for(int i = 1; i < 6; ++i)
		texture->uploadCubemap(texture->format, texture->type, false, (Uint8**)hdre->getFaces(i), GL_RGBA32F, i);
	return texture;
	*/
	return NULL;
}