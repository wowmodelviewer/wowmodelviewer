#pragma once

#include <string>

/// @brief Base class for all scene-graph nodes in the component hierarchy.
///
/// Supports parent–child relationships, reference counting, and naming.
/// Derived classes (Container, GameFile, WoWItem, etc.) extend this to
/// form the application's object graph.
class Component
{
public:
	Component();
	virtual ~Component();

	/// @brief Add a child component to this node.
	virtual bool addChild(Component*);

	/// @brief Remove a child component from this node.
	virtual bool removeChild(Component*);

	virtual void removeAllChildren()
	{
	}

	virtual unsigned int nbChildren() const { return 0; }

	virtual bool findChildComponent(Component* /* component */, bool /* recursive */) { return false; }
	virtual Component* getChild(unsigned int /* index */) { return nullptr; }
	virtual const Component* getChild(unsigned int /* index */) const { return nullptr; }

	/// @brief Set the parent of this component.
	void setParentComponent(Component*);

	/// @brief Called after the parent has been set; override for custom logic.
	virtual void onParentSet(Component*);

	/// @brief Get the parent component (const).
	const Component* parent() const { return m_p_parent; }

	/// @brief Get the parent component.
	Component* parent() { return m_p_parent; }

	/// @brief Walk up the parent chain and return the first ancestor of the given type.
	/// @tparam DataType  The type to search for (must be a Component subclass).
	/// @return Pointer to the ancestor, or nullptr if none found.
	template <class DataType>
	const DataType* firstParentOfType();

	/// @brief Increment the reference counter.
	void ref();

	/// @brief Decrement the reference counter; deletes this when it reaches zero.
	void unref();

	// Name management
	void setName(const std::string& name);
	std::string name() const;
	virtual void onNameChanged();

	// misc
	void print(int l_depth = 0);
	// overlaod in inheritted classes to perform specific stuff at display time
	virtual void doPrint();

	// copy
	void copy(const Component& component, bool /* recursive*/);

private:
	Component* m_p_parent;

	unsigned int m_refCounter;

	std::string m_name;
};

template <class DataType>
const DataType* Component::firstParentOfType()
{
	if (parent() != nullptr)
	{
		DataType* l_p_parent = dynamic_cast<DataType*>(parent());
		if (l_p_parent != nullptr)
			return l_p_parent;
		else
			return parent()->firstParentOfType<DataType>();
	}
	return nullptr;
}
