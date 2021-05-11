/*#ifndef LIGHT_H
#define LIGHT_H

#include "scene.h"

class Shader;
class FBO;
class Camera;
class Texture;

namespace GTR {

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
		virtual void uploadUniforms(Shader* &shader);
		virtual void lightVisible();
		virtual void orientCam();
	};


};


#endif

*/