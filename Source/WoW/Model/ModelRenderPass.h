#pragma once

#include "types.h"

#include "glm/glm.hpp"

class WoWModel;

/// @brief Represents a single render pass (material + geometry) for an M2 model geoset.
class ModelRenderPass
{
public:
	ModelRenderPass(WoWModel*, int geo);

	bool useTex2, useEnvMap, cull, trans, unlit, noZWrite, billboard;  ///< Render state flags.

	int16 texanim, color, opacity, blendmode, specialTex;  ///< Material property indices.
	uint16 tex;  ///< Texture index.

	bool swrap, twrap;  ///< Texture wrapping modes (S and T).

	glm::vec4 ocol, ecol;  ///< Output and emissive colours.

	WoWModel* model;  ///< Owning model.

	int geoIndex;  ///< Geoset index this pass draws.

	/// @brief Initialise render state from the model's material data.
	bool init();

	/// @brief Execute the render pass.
	void render(bool animated);

	/// @brief Clean up render state after drawing.
	void deinit();

	static const uint16 INVALID_TEX = 50000;  ///< Sentinel value for an unset texture.

	/*
	  bool operator< (const ModelRenderPass &m) const
	  {
	    // Probably not 100% right, but seems to work better than just geoset sorting.
	    // Blend mode mostly takes into account transparency and material - Wain
	    if (trans == m.trans)
	    {
	      if (blendmode == m.blendmode)
	        return (geoIndex < m.geoIndex);
	      return blendmode < m.blendmode;
	    }
	    return (trans < m.trans);
	  }
	*/
};
