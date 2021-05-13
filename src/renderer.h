#pragma once
#include "prefab.h"
#include "fbo.h"

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

	class Prefab;
	class Material;

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

		eRenderMode render_mode;
		ePipelineMode pipeline_mode;

		std::vector<RenderCall*> renderCalls;
		std::vector<RenderCall*> renderCalls_Blending;

		FBO fbo_gbuffers;

		int current_mode;
		int current_mode_pipeline;

		const char* optionsText[3] = { {"Texture"},{"Multipass"},{"SinglePass"} };

		const char* optionsTextPipeline[2] = {{"Forward"},{"Deferred"}};

		bool renderingShadows;

		bool cast_shadows;

		bool showGbuffers;

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

		void renderForward(Scene* scene, std::vector<RenderCall*>& rc, Camera* camera);

		/**********************************************************************************************/
		//deferred
		void collectRenderCalls(GTR::Scene* scene, Camera* camera);

		void renderDeferred(Scene* scene, std::vector<RenderCall*>& rc, Camera* camera);

		void showgbuffers(Camera* camera);

		void multipassUniformsDeferred(LightEntity* light, Camera* camera, int iteration);

		void multipassDeferred(Camera* camera);

	};

	Texture* CubemapFromHDRE(const char* filename);

};