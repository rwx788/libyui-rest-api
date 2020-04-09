/*
  Copyright (C) 2017 SUSE LLC

  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) version 3.0 of the License. This library
  is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
  License for more details. You should have received a copy of the GNU
  Lesser General Public License along with this library; if not, write
  to the Free Software Foundation, Inc., 51 Franklin Street, Fifth
  Floor, Boston, MA 02110-1301 USA
*/

#include "YCheckBox.h"
#include "YComboBox.h"
#include "YDialog.h"
#include "YDumbTab.h"
#include "YInputField.h"
#include "YIntField.h"
#include "YItemSelector.h"
#include "YMenuButton.h"
#include "YMultiLineEdit.h"
#include "YPushButton.h"
#include "YRadioButton.h"
#include "YRichText.h"
#include "YTable.h"
#include "YTree.h"
#include "YTreeItem.h"
#include "YSelectionBox.h"

#include <codecvt>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <string>
#include <boost/algorithm/string.hpp>

#include "YHttpWidgetsActionHandler.h"

void YHttpWidgetsActionHandler::body(struct MHD_Connection* connection,
    const char* url, const char* method, const char* upload_data,
    size_t* upload_data_size, std::ostream& body, bool *redraw)
{
    if ( YDialog::topmostDialog(false) )
    {
        WidgetArray widgets;

        const char* label = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "label");
        const char* id = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "id");
        const char* type = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "type");

        if ( label || id || type )
        {
            widgets = YWidgetFinder::find(label, id, type);
        }
        else
        {
            widgets = YWidgetFinder::all();
        }

        if ( widgets.empty() )
        {
            body << "{ \"error\" : \"Widget not found\" }" << std::endl;
            _error_code = MHD_HTTP_NOT_FOUND;
        }
        else if ( const char* action = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "action") )
        {
            if( widgets.size() != 1 )
            {
                body << "{ \"error\" : \"Multiple widgets found to act on, try using multicriteria search (label+id+type)\" }" << std::endl;
                _error_code = MHD_HTTP_NOT_FOUND;
            }
            _error_code = do_action(widgets[0], action, connection, body);

            // the action possibly changed something in the UI, signalize redraw needed
            if ( redraw && _error_code == MHD_HTTP_OK )
                *redraw = true;
        }
        else
        {
            body << "{ \"error\" : \"Missing action parameter\" }" << std::endl;
            _error_code = MHD_HTTP_NOT_FOUND;
        }
    }
    else {
        body << "{ \"error\" : \"No dialog is open\" }" << std::endl;
        _error_code = MHD_HTTP_NOT_FOUND;
    }
}

std::string YHttpWidgetsActionHandler::contentEncoding()
{
    return "application/json";
}

int YHttpWidgetsActionHandler::do_action(YWidget *widget, const std::string &action, struct MHD_Connection *connection, std::ostream& body) {

    yuiMilestone() << "Starting action: " << action << std::endl;

    // TODO improve this, maybe use better names for the actions...

    // press a button
    if ( action == "press" )
    {
        yuiMilestone() << "Received action: press" << std::endl;
        if (dynamic_cast<YPushButton*>(widget))
        {
            return action_handler<YPushButton>(widget, [] (YPushButton *button)
            {
                yuiMilestone() << "Pressing button \"" << button->label() << '"' << std::endl;
                button->setKeyboardFocus();
                button->activate();
            } );
        }
        else if ( dynamic_cast<YRichText*>(widget) )
        {
            std::string value;
            if ( const char* val = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "value") )
                value = val;
            return action_handler<YRichText>(widget, [&] (YRichText *rt) {
                yuiMilestone() << "Activating hyperlink on richtext: \"" << value << '"' << std::endl;
                rt->setKeyboardFocus();
                rt->activateLink(value);
            } );
        }
        else if( dynamic_cast<YMenuButton*>(widget) )
        {
            std::string value;
            if ( const char* val = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "value") )
                value = val;
            return action_handler<YMenuButton>(widget, [&] (YMenuButton *mb) {
                // Vector of string to store path to the tree item
                std::vector<std::string> path;
                boost::split( path, value, boost::is_any_of( TreePathDelimiter ) );
                YMenuItem * item = mb->findItem( path );
                if ( item )
                {
                    yuiMilestone() << "Activating Item by path :" << value << " in \"" << mb->label() << "\" MenuButton" << std::endl;
                    mb->setKeyboardFocus();
                    mb->activateItem( item );
                }
                else
                {
                    body << "Item with path: \"" << value << "\" cannot be found in the MenuButton widget" << std::endl;
                    throw YUIException("Item cannot be found in the MenuButton widget");
                }
            } );
        }
        else
        {
            body << "Action is not supported for the selected widget: " << widget->widgetClass() << std::endl;
            return MHD_HTTP_NOT_FOUND;
        }
    }
    // check a checkbox
    else if ( action == "check" )
    {
        if ( dynamic_cast<YCheckBox*>(widget) )
        {
            return action_handler<YCheckBox>(widget, [] (YCheckBox *checkbox) {
                if (checkbox->isChecked()) return;
                yuiMilestone() << "Checking \"" << checkbox->label() << '"' << std::endl;
                checkbox->setKeyboardFocus();
                checkbox->setChecked(true);
            } );
        }
        else if( dynamic_cast<YItemSelector*>(widget) )
        {
            std::string value;
            if (const char* val = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "value"))
                value = val;

            return get_item_selector_handler(widget, value, body, 1);
        }
        else
        {
            body << "Action is not supported for the selected widget" << widget->widgetClass() << std::endl;
            return MHD_HTTP_NOT_FOUND;
        }
    }
    // uncheck a checkbox
    else if ( action == "uncheck" )
    {
        if ( dynamic_cast<YCheckBox*>(widget) )
        {
            return action_handler<YCheckBox>(widget, [] (YCheckBox *checkbox) {
                if (!checkbox->isChecked()) return;
                yuiMilestone() << "Unchecking \"" << checkbox->label() << '"' << std::endl;
                checkbox->setKeyboardFocus();
                checkbox->setChecked(false);
            } );
        }
        else if( dynamic_cast<YItemSelector*>(widget) )
        {
            std::string value;
            if ( const char* val = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "value") )
                value = val;

            return get_item_selector_handler(widget, value, body, 0);
        }
        else
        {
            body << "Action is not supported for the selected widget" << widget->widgetClass() << std::endl;
            return MHD_HTTP_NOT_FOUND;
        }
    }
    // toggle a checkbox (reverse the state)
    else if ( action == "toggle" ) {
        if ( dynamic_cast<YCheckBox*>(widget) )
        {
            return action_handler<YCheckBox>(widget, [] (YCheckBox *checkbox) {
                yuiMilestone() << "Toggling \"" << checkbox->label() << '"' << std::endl;
                checkbox->setKeyboardFocus();
                checkbox->setChecked(!checkbox->isChecked());
            } );
        }
        else if( dynamic_cast<YItemSelector*>(widget) )
        {
            std::string value;
            if ( const char* val = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "value") )
                value = val;

            return get_item_selector_handler(widget, value, body);
        }
        else
        {
            body << "Action is not supported for the selected widget" << widget->widgetClass() << std::endl;
            return MHD_HTTP_NOT_FOUND;
        }
    }
    // enter input field text
    else if ( action == "enter_text" )
    {
        std::string value;
        if ( const char* val = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "value") )
            value = val;

        if ( dynamic_cast<YInputField*>(widget) )
        {
            return action_handler<YInputField>(widget, [&] (YInputField *input) {
                yuiMilestone() << "Setting value for InputField \"" << input->label() << '"' << std::endl;
                input->setKeyboardFocus();
                input->setValue(value);
            } );
        }
        else if ( dynamic_cast<YIntField*>(widget) )
        {
            return action_handler<YIntField>(widget, [&] (YIntField *input) {
                yuiMilestone() << "Setting value for YIntField \"" << input->label() << '"' << std::endl;
                input->setKeyboardFocus();
                input->setValue(atoi(value.c_str()));
            } );
        }
        else if ( dynamic_cast<YMultiLineEdit*>(widget) )
        {
            return action_handler<YMultiLineEdit>(widget, [&] (YMultiLineEdit *input) {
                yuiMilestone() << "Setting value for YMultiLineEdit \"" << input->label() << '"' << std::endl;
                input->setKeyboardFocus();
                input->setValue(value);
            } );
        }
        else
        {
            body << "Action is not supported for the selected widget: " << widget->widgetClass() << std::endl;
            return MHD_HTTP_NOT_FOUND;
        }
    }
    else if ( action == "select" )
    {
        std::string value;
        if (const char* val = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "value"))
            value = val;

        if ( dynamic_cast<YComboBox*>(widget) )
        {
            return action_handler<YComboBox>(widget, [&] (YComboBox *cb) {
                yuiMilestone() << "Activating ComboBox \"" << cb->label() << '"' << std::endl;
                cb->setKeyboardFocus();
                // cb->setValue(value);
                YItem * item = cb->findItem(value);
                if ( item )
                {
                        yuiMilestone() << "Activating Combobox \"" << cb->label() << '"' << std::endl;
                        cb->selectItem(item);
                        cb->activate();
                }
                else
                {
                    body << '"' << value << "\" item cannot be found in the table" << std::endl;
                    throw YUIException("Item cannot be found in the table");
                }
            } );
        }
        else if( dynamic_cast<YTable*>(widget) )
        {
            int column_id = 0;
            if ( const char* val = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "column") )
                column_id = atoi(val);
            return action_handler<YTable>(widget, [&] (YTable *tb) {
                YItem * item = tb->findItem(value, column_id);
                if ( item )
                {
                        yuiMilestone() << "Activating Table \"" << tb->label() << '"' << std::endl;
                        tb->setKeyboardFocus();
                        tb->selectItem(item);
                }
                else
                {
                    body << '"' << value << "\" item cannot be found in the table" << std::endl;
                    throw YUIException("Item cannot be found in the table");
                }
            } );
        }
        else if( dynamic_cast<YTree*>(widget) )
        {
            return action_handler<YTree>(widget, [&] (YTree *tree) {
                // Vector of string to store path to the tree item
                std::vector<std::string> path;
                boost::split( path, value, boost::is_any_of( TreePathDelimiter ) );
                YItem * item = tree->findItem( path );
                if (item)
                {
                    yuiMilestone() << "Activating Tree Item \"" << item->label() << '"' << std::endl;
                    tree->setKeyboardFocus();
                    tree->selectItem(item);
                    tree->activate();
                }
                else
                {
                    body << '"' << value << "\" item cannot be found in the tree" << std::endl;
                    throw YUIException("Item cannot be found in the tree");
                }
            } );
        }
        else if ( dynamic_cast<YDumbTab*>(widget) )
        {
            return action_handler<YDumbTab>(widget, [&] (YDumbTab *tab) {
                YItem * item = tab->findItem( value );
                if ( item )
                {
                    yuiMilestone() << "Activating Tree Item \"" << item->label() << '"' << std::endl;
                    tab->setKeyboardFocus();
                    tab->selectItem(item);
                    tab->activate();
                }
                else
                {
                    body << '"' << value << "\" item cannot be found in the tree" << std::endl;
                    throw YUIException("Item cannot be found in the tree");
                }
            } );
        }
        else if( dynamic_cast<YRadioButton*>(widget) )
        {
            return action_handler<YRadioButton>(widget, [&] (YRadioButton *rb) {
                yuiMilestone() << "Activating RadioButton \"" << rb->label() << '"' << std::endl;
                rb->setKeyboardFocus();
                rb->setValue(true);
            } );
        }
        else if( dynamic_cast<YSelectionBox*>(widget) )
        {
            return action_handler<YSelectionBox>(widget, [&] (YSelectionBox *sb) {
                YItem * item = sb->findItem( value );
                if ( item )
                {
                    yuiMilestone() << "Activating selection box \"" << sb->label() << '"' << std::endl;
                    sb->setKeyboardFocus();
                    sb->selectItem(item);
                }
                else
                {
                    body << '"' << value << "\" item cannot be found in the selection box" << std::endl;
                    throw YUIException("Item cannot be found in the selection box");
                }
            } );
        }
        else if( dynamic_cast<YItemSelector*>(widget) )
        {
            return get_item_selector_handler(widget, value, body, 1);
        }
        else
        {
            body << "Action is not supported for the selected widget" << widget->widgetClass() << std::endl;
            return MHD_HTTP_NOT_FOUND;
        }
    }
    // TODO: more actions
    // else if (action == "enter") {
    // }
    else
    {
        body << "{ \"error\" : \"Unknown action\" }" << std::endl;
        return MHD_HTTP_NOT_FOUND;
    }

    return MHD_HTTP_OK;
}

int YHttpWidgetsActionHandler::get_item_selector_handler(YWidget *widget, const std::string &value, std::ostream& body, const int state) {
    return action_handler<YItemSelector>(widget, [&] (YItemSelector *is) {
        YItem * item = is->findItem( value );
        if ( item )
        {
            yuiMilestone() << "Activating item selector with item \"" << value << '"' << std::endl;
            is->setKeyboardFocus();
            // Toggle in case state is undefined
            bool select = state < 0  ? !item->selected() :
                          state == 0 ? false :
                                       true;
            if( state < 0 )
            {
                select = !item->selected();
            }
            else
            {
                select = state == 0 ? false : true;
            }
            item->setSelected( select );
            is->selectItem( item, select );
            is->activateItem( item );
        }
        else
        {
            body << '"' << value << "\" item cannot be found in the item selector" << std::endl;
            throw YUIException("Item cannot be found in the item selector");
        }
    } );
}
