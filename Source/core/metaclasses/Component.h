#pragma once

#include <QString>

#ifdef _WIN32
#    ifdef BUILDING_CORE_DLL
#        define _COMPONENT_API_ __declspec(dllexport)
#    else
#        define _COMPONENT_API_ __declspec(dllimport)
#    endif
#else
#    define _COMPONENT_API_
#endif

class _COMPONENT_API_ Component
{
public:
	Component();
	virtual ~Component();

	virtual bool addChild(Component*);
	virtual bool removeChild(Component*);

	virtual void removeAllChildren()
	{
	}

	virtual unsigned int nbChildren() const { return 0; }

	virtual bool findChildComponent(Component* /* component */, bool /* recursive */) { return false; }
	virtual Component* getChild(unsigned int /* index */) { return nullptr; }
	virtual const Component* getChild(unsigned int /* index */) const { return nullptr; }

	// parent management
	void setParentComponent(Component*);
	virtual void onParentSet(Component*);
	const Component* parent() const { return m_p_parent; }
	Component* parent() { return m_p_parent; }

	template <class DataType>
	const DataType* firstParentOfType();

	// auto delete management
	void ref();
	void unref();

	// Name management
	void setName(const QString& name);
	QString name() const;
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

	QString m_name;
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
