#include "Component.h"
#include <iostream>
#include <string>

Component::Component() : m_p_parent(nullptr), m_refCounter(0)
{
	m_name = "Component";
}

Component::~Component() = default;

bool Component::addChild(Component*)
{
	return false;
}

bool Component::removeChild(Component*)
{
	return false;
}

void Component::ref()
{
	m_refCounter++;
}

void Component::unref()
{
	m_refCounter--;
	if (m_refCounter <= 0)
	{
		delete this;
	}
}

void Component::onParentSet(Component*)
{
}

void Component::setParentComponent(Component* a_p_parent)
{
	m_p_parent = a_p_parent;
	onParentSet(m_p_parent);
}

void Component::setName(const QString& name)
{
	m_name = name;
	onNameChanged();
}

QString Component::name() const
{
	return m_name;
}

void Component::onNameChanged()
{
}

void Component::print(int a_depth /*= 0*/)
{
	for (int i = 0; i < a_depth - 1; i++)
	{
		std::cout << "  ";
	}

	if (m_p_parent != nullptr)
	{
		std::cout << "|-";
	}

	if (nbChildren() != 0)
	{
		std::cout << "+ ";
	}
	else
	{
		std::cout << " ";
	}

	doPrint();

	a_depth++;

	for (unsigned int i = 0; i < nbChildren(); i++)
	{
		getChild(i)->print(a_depth);
	}
}

void Component::copy(const Component& component, bool /* recursive*/)
{
	m_name = component.m_name;
}

void Component::doPrint()
{
	std::cout << m_name.toStdString() << " (address : " << std::hex << this << ")" << std::endl;
}
