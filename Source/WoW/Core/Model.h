#pragma once

#define _MODEL_API_

/// @brief Abstract base interface for all 3D model types.
class _MODEL_API_ Model
{
public:
	Model() = default;

	virtual ~Model() = 0;
};
