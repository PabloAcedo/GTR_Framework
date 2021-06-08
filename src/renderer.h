#pragma once
#include "prefab.h"
#include "fbo.h"
#include "sphericalharmonics.h"

//forward declarations
class Camera;
class Shader;


namespace GTR {

	enum eRenderMode {
		TEXTURE,
		LIGHT_MULTI,
		LIGHT_SINGLE,
		GBUFFERS,
		SHADOWS
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

		//tonemap
		float avg_lum;
		float lum_white;
		float gamma;
		float scale_tonemap;
		bool apply_tonemap;

		//irradiance
		FBO* irr_fbo;

		//bools
		bool renderingShadows;
		bool cast_shadows;
		bool showGbuffers;
		bool showSSAO;



		Renderer();

		/**********************************************************************************************/
		//forward
		void commonUniforms(Shader*& shader, const Matrix44 model, GTR::Material* material, Camera* camera, Mesh* mesh, bool fromOther); //texture

		void multipassUniforms(LightEntity* light, Shader*& shader, const Matrix44 model, GTR::Material* material, Camera* camera, Mesh* mesh, int iteration); //multipass

		void multipassRendering(Shader*& shader, const Matrix44 model, GTR::Material* material, Camera* camera, Mesh* mesh); //multipass renderer

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
		void renderMeshWithMaterial(eRenderMode mode, const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

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

		void renderAmbient();

		/**********************************************************************************************/
		void renderInMenu();
		void renderFinal();

		
		/**********************************************************************************************/
		//irradiance
		void computeProbe(Scene* scene, sProbe& p);

		void updateIrradianceCache(GTR::Scene* scene);

		void renderProbe(Vector3 pos, float size, float* coeffs);

		void computeProbes(Scene* scene);
	};

	Texture* CubemapFromHDRE(const char* filename);

};