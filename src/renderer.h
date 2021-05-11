#pragma once
#include "prefab.h"

//forward declarations
class Camera;
class Shader;

namespace GTR {

	enum eRenderMode {
		TEXTURE,
		LIGHT_MULTI,
		LIGHT_SINGLE
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

		std::vector<RenderCall*> renderCalls;

		int current_mode;

		const char* optionsText[3] = { {"Texture"},{"Multipass"},{"SinglePass"} };

		bool renderingShadows;
		//add here your functions
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
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		void changeRenderMode();
	};

	Texture* CubemapFromHDRE(const char* filename);

};