#pragma once

#include "manager.h"

/// @brief Manages loaded WoW model instances, providing animation reset, emitter update, and cleanup.
class ModelManager : public SimpleManager
{
public:
	/// @brief Add a model from the given game file and return its handle.
	int add(GameFile*);

	ModelManager() : v(0)
	{
	}

	int v;  ///< Internal version counter.

	/// @brief Reset animations on all managed models.
	void resetAnim();
	/// @brief Update particle and ribbon emitters on all managed models.
	void updateEmitters(float dt);
	/// @brief Remove and destroy all managed models.
	void clear();
};
