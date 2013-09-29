#include "BsGUIContextMenu.h"
#include "BsGUIDropDownBoxManager.h"
#include "BsGUIManager.h"

using namespace CamelotFramework;

namespace BansheeEngine
{
	GUIContextMenu::GUIContextMenu()
		:GUIMenu(), mContextMenuOpen(false)
	{

	}

	GUIContextMenu::~GUIContextMenu()
	{
		close();
	}

	void GUIContextMenu::open(const Int2& position, GUIWidget& widget)
	{
		GUIDropDownAreaPlacement placement = GUIDropDownAreaPlacement::aroundPosition(position);

		GameObjectHandle<GUIDropDownBox> dropDownBox = GUIDropDownBoxManager::instance().openDropDownBox(widget.getTarget(), widget.getOwnerWindow(), 
			placement, getDropDownData(), widget.getSkin(), GUIDropDownType::ContextMenu, boost::bind(&GUIContextMenu::onMenuClosed, this));

		GUIManager::instance().enableSelectiveInput(boost::bind(&GUIContextMenu::close, this));
		GUIManager::instance().addSelectiveInputWidget(dropDownBox.get());

		mContextMenuOpen = true;
	}

	void GUIContextMenu::close()
	{
		if(mContextMenuOpen)
		{
			GUIDropDownBoxManager::instance().closeDropDownBox();
			GUIManager::instance().disableSelectiveInput();
			mContextMenuOpen = false;
		}
	}

	void GUIContextMenu::onMenuClosed()
	{
		GUIManager::instance().disableSelectiveInput();
		mContextMenuOpen = false;
	}
}