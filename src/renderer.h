#pragma once
#include "prefab.h"
#include "fbo.h"
#include "sphericalharmonics.h"

//forward declarations
class Camera;
class Shader;
class Texture;
class HDRE;

namespace GTR {

	enum eRenderMode {
		TEXTURE,
		LIGHT_MULTI,
		LIGHT_SINGLE,
		GBUFFERS,
		SHADOWS, 
		REFLECTION
	};

	enum ePipelineMode {
		FORWARD,
		DEFERRED
	};

	enum eIlumMode {
		PHONG,
		PBR
	};

	class Prefab;
	class Material;
	

	class SSAOFX {
	public:

		std::vector<Vector3> points;
		FBO ao_fbo;
		float bias_slider;
		float radius_slider;
		float max_distance_slider;
		SSAOFX();
		void compute(Texture* depth_buffer, Texture* normal_buffer, Camera* camera, Texture* output);
	};

	class RenderCall {
	public:
		Matrix44 model;
		Mesh* mesh;

		Material* material;
		float distance2Cam;
		Texture* reflection;
		RenderCall(Matrix44 model, Mesh* mesh, Material* material);
	};
	
	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{

	public:

		//modes
		eRenderMode render_mode;
		ePipelineMode pipeline_mode;
		eIlumMode ilum_mode;
		int current_mode;
		int current_mode_pipeline;
		int current_mode_ilum;

		const char* optionsText[3] = { {"Texture"},{"Multipass"},{"SinglePass"} };

		const char* optionsTextPipeline[2] = { {"Forward"},{"Deferred"} };

		const char* optionsTextIlum[2] = { {"Phong"},{"PBR"} };

		//rendercalls
		std::vector<RenderCall*> renderCalls;
		std::vector<RenderCall*> renderCalls_Blending;

		//deferred
		FBO fbo_gbuffers;
		FBO scene_fbo;

		//ssao
		SSAOFX ssao;
		Texture* ao_map;
		bool apply_ssao;
		FBO final_render_fbo;
		bool applyAA;

		//tonemap
		float avg_lum;
		float lum_white;
		float gamma;
		float scale_tonemap;
		bool apply_tonemap;

		//irradiance
		FBO* irr_fbo;
		FBO irr_map_fbo;
		bool show_irr_tex;
		bool showProbesGrid;
		bool apply_irr;
		float irradiance_intensity;


		//reflections
		bool show_reflection_probes;
		Texture* currentReflection;
		FBO reflection_fbo;
		FBO* cRefl_fbo;
		bool apply_reflections;
		bool first_it;

		//bools
		bool renderingShadows;
		bool cast_shadows;
		bool showGbuffers;
		bool showSSAO;

		//volume rendering
		FBO fog_fbo;
		bool apply_fog;
		float fog_density;
		int vol_iterations;

		//postpo
		FBO blur_fbo;
		FBO* bloom;
		bool apply_bloom;
		float bloom_threshold;
		int bloom_size;
		bool show_bloom_tex;
		float bloom_intensity;

		//decals
		FBO decals_fbo;

		//DOF
		float dof_max_dist;
		float dof_min_dist;
		bool apply_dof;

		//chromatic aberration
		FBO chromatic_fbo;
		bool apply_chromatic_aberration;
		float max_distortion;
		Renderer();

		/**********************************************************************************************/
		//forward
		void renderSkybox(Texture* skybox, Camera* camera, bool isForward);

		void commonUniforms(Shader*& shader, const Matrix44 model, GTR::Material* material, Camera* camera, Mesh* mesh, bool fromOther); //texture

		void multipassUniforms(LightEntity* light, Shader*& shader, const Matrix44 model, GTR::Material* material, Camera* camera, Mesh* mesh, int iteration, Texture* cubemap); //multipass

		void multipassRendering(Shader*& shader, const Matrix44 model, GTR::Material* material, Camera* camera, Mesh* mesh, Texture* cubemap); //multipass renderer

		void uploadExtraMap(Shader*& shader, Texture* texture, const char* uniform_name, const char* bool_name, int tex_slot);

		void singlepassUniforms(Shader*& shader, const Matrix44 model, GTR::Material* material, Camera* camera, Mesh* mesh);

		void renderShadowMaps(Scene* scene);

		void displayShadowMap();

		//renders several elements of the scene
		void renderScene(GTR::Scene* scene, Camera* camera);
	
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(eRenderMode mode, const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera, Texture* cubemap);

		void changeRenderMode();

		void changePipelineMode();
		void changeIlumMode();

		void renderForward(Scene* scene, std::vector<RenderCall*>& rc, Camera* camera);

		/**********************************************************************************************/
		//deferred
		void collectRenderCalls(GTR::Scene* scene, Camera* camera);

		void renderDeferred(Scene* scene, std::vector<RenderCall*>& rc, Camera* camera);

		void showgbuffers(Camera* camera);

		void multipassUniformsDeferred(LightEntity* light, Camera* camera, int iteration);

		void multipassDeferred(Camera* camera);

		void renderAmbient(Camera* camera);

		/**********************************************************************************************/
		void renderInMenu();
		void renderFinal(Texture* tex);
		Texture* AAFX(Texture* tex);
		
		/**********************************************************************************************/
		//irradiance
		void computeProbe(Scene* scene, sProbe& p, int iteration);

		void updateIrradianceCache(GTR::Scene* scene);

		void renderProbe(Vector3 pos, float size, float* coeffs);

		void computeProbes(Scene* scene);

		void irradianceMap(Texture* depth_buffer, Texture* normal_buffer, Camera* camera);

		/**********************************************************************************************/
		//reflections
		void updateReflectionProbes(Scene* scene);

		void captureReflectionProbe(Scene* scene, reflectionProbeEntity*& probe);

		void renderReflectionProbes(Scene* scene, Camera* camera);

		void addReflectionsToScene(Camera* camera);
		/**********************************************************************************************/
		//volume rendering
		void render_fog(Scene* scene, Camera* camera);
		/**********************************************************************************************/
		//postpo FX
		Texture* gaussian_blur(Texture* tex, bool horizontal);
		Texture* blur_image(Texture* tex, int iterations);
		Texture* bloom_effect(Texture* blurred_tex, int w, int h);
		/**********************************************************************************************/
		//Decals
		void renderDecals(Scene* scene, Camera* camera);
		void copyFboTextures(FBO& source, FBO& destination, int textures_num);
		/**********************************************************************************************/
		//DOF
		void depthOfField(Texture* in_focus, Texture* out_focus, Camera* camera);
		/**********************************************************************************************/
		//crhomatic aberration
		void chromatic_aberration();
	};

	Texture* CubemapFromHDRE(const char* filename);

};