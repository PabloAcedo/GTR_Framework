#ifndef SCENE_H
#define SCENE_H

#define MAX_LIGHTS 5

#include "framework.h"
#include "camera.h"
#include <string>

//forward declaration
class cJSON; 
class Shader;
class FBO;
class Texture;

//our namespace
namespace GTR {



	enum eEntityType {
		NONE = 0,
		PREFAB = 1,
		LIGHT = 2,
		CAMERA = 3,
		REFLECTION_PROBE = 4,
		DECALL = 5
	};

	class Scene;
	class Prefab;

	//represents one element of the scene (could be lights, prefabs, cameras, etc)
	class BaseEntity
	{
	public:
		Scene* scene;
		std::string name;
		eEntityType entity_type;
		Matrix44 model;
		bool visible;
		BaseEntity() { entity_type = NONE; visible = true; }
		virtual ~BaseEntity() {}
		virtual void renderInMenu();
		virtual void configure(cJSON* json) {}
	};

	//represents one prefab in the scene
	class PrefabEntity : public GTR::BaseEntity
	{
	public:
		std::string filename;
		Prefab* prefab;
		
		PrefabEntity();
		virtual void renderInMenu();
		virtual void configure(cJSON* json);
	};

	enum eLightType {
		POINT = 0,
		SPOT = 1,
		DIRECTIONAL = 2
	};

	class LightEntity : public GTR::BaseEntity {

	public:
		eLightType light_type;
		vec3 color; vec3 auxcolor;
		bool colorsaved;
		float intensity;
		float max_dist;
		float cone_angle;
		float area_size;
		float spotExp;

		//shadows
		FBO* fbo = NULL;
		Camera* cam;
		float bias;
		bool show_shadowmap;

		LightEntity();
		~LightEntity();

		virtual void renderInMenu();
		virtual void configure(cJSON* json);
		virtual void setColor(vec3 color);
		virtual void uploadUniforms(Shader*& shader);
		virtual void lightVisible();
		virtual void orientCam();
	};

	//contains all entities of the scene
	class Scene
	{
	public:
		static Scene* instance;

		Vector3 background_color;
		Vector3 ambient_light;
		Camera main_camera;
		std::vector<LightEntity*> lights;

		//single pass light data
		Vector3 light_pos[MAX_LIGHTS];
		Vector3 light_color[MAX_LIGHTS];
		float l_intensity[MAX_LIGHTS];
		float l_max_dist[MAX_LIGHTS];
		float l_cone_angle[MAX_LIGHTS];
		float l_spotExp[MAX_LIGHTS];
		Vector3 light_direction[MAX_LIGHTS];
		int light_type[MAX_LIGHTS];

		Scene();

		std::string filename;
		std::vector<BaseEntity*> entities;

		void clear();
		void addEntity(BaseEntity* entity);

		bool load(const char* filename);
		BaseEntity* createEntity(std::string type);

		void updateLights(); //for singlePass
	};

};

#endif