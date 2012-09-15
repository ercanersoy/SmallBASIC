// This file is part of SmallBASIC
//
// Copyright(C) 2001-2012 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0 or later
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <ma.h>

#include <MAUtil/String.h>
#include <MAUtil/Vector.h>

#include "config.h"
#include "common/sys.h"
#include "common/var.h"
#include "common/kw.h"
#include "common/pproc.h"
#include "common/device.h"
#include "common/smbas.h"
#include "common/keymap.h"
#include "common/blib_ui.h"

#include "platform/mosync/controller.h"
#include "platform/mosync/ansiwidget.h"
#include "platform/mosync/utils.h"
#include "platform/mosync/form_ui.h"

// width and height fudge factors for when button w+h specified as -1
#define BN_W  16
#define BN_H   8
#define RAD_W 22
#define RAD_H  0

extern Controller *controller;
Form *form;

// convert a basic array into a String
void array_to_string(String &s, var_t *v) {
  for (int i = 0; i < v->v.a.size; i++) {
    var_t *el_p = (var_t *)(v->v.a.ptr + sizeof(var_t) * i);
    if (el_p->type == V_STR) {
      const char *p = (const char *)el_p->v.p.ptr;
      s.append(p, strlen(p));
      s.append("\n", 1);
    } else if (el_p->type == V_INT) {
      char buff[40];
      sprintf(buff, VAR_INT_FMT "\n", el_p->v.i);
      s.append(buff, strlen(buff));
    } else if (el_p->type == V_ARRAY) {
      array_to_string(s, el_p);
    }
  }
}

// implements abstract StringList as a list of strings
struct ListModel : IFormWidgetListModel {
  Vector<String *> list;
  int focusIndex;

  ListModel(const char *items, var_t *v) {
    create(items, v);
  }

  virtual ~ListModel() {
    clear();
  }

  void clear() {
    Vector_each(String*, it, list) {
      delete (*it);
    }
    list.clear();
    focusIndex = -1;
  }

  void create(const char *items, var_t *v) {
    if (v && v->type == V_ARRAY) {
      fromArray(items, v);
    } else {
      // construct from a string like "Easy|Medium|Hard"
      int item_index = 0;
      int len = items ? strlen(items) : 0;
      for (int i = 0; i < len; i++) {
        const char *c = strchr(items + i, '|');
        int end_index = c ? c - items : len;
        if (end_index > 0) {
          String *s = new String(items + i, end_index - i);
          list.add(s);
          i = end_index;
          if (v != 0 && v->type == V_STR && v->v.p.ptr &&
              strcasecmp((const char *)v->v.p.ptr, s->c_str()) == 0) {
            focusIndex = item_index;
          }
          item_index++;
        }
      }
    }
  }

  // construct from an array of values
  void fromArray(const char *caption, var_t *v) {
    for (int i = 0; i < v->v.a.size; i++) {
      var_t *el_p = (var_t *)(v->v.a.ptr + sizeof(var_t) * i);
      if (el_p->type == V_STR) {
        list.add(new String((const char *)el_p->v.p.ptr));
        if (caption && strcasecmp((const char *)el_p->v.p.ptr, caption) == 0) {
          focusIndex = i;
        }
      } else if (el_p->type == V_INT) {
        char buff[40];
        sprintf(buff, VAR_INT_FMT, el_p->v.i);
        list.add(new String(buff));
      } else if (el_p->type == V_ARRAY) {
        fromArray(caption, el_p);
      }
    }
  }

  // return the text at the given index
  const char *getTextAt(int index) {
    const char *s = 0;
    if (index > -1 && index < list.size()) {
      s = list[index]->c_str();
    }
    return s;
  }

  // returns the model index corresponding to the given string
  int getIndex(const char *t) {
    int size = list.size();
    for (int i = 0; i < size; i++) {
      if (!strcasecmp(list[i]->c_str(), t)) {
        return i;
      }
    }
    return -1;
  }

  // return the number of rows under the given parent
  int rowCount() const {
    return list.size();
  }

  int selectedIndex() const {
    return focusIndex;
  }
  
  void setSelectedIndex(int index) {
    focusIndex = index;
  }
};

// Form constructor
Form::Form() :
  mode(m_init),
  cmd(0), 
  var(0),
  kb_handle(false),
  prev_x(0),
  prev_y(0) {
} 

Form::~Form() {
  Vector_each(WidgetDataPtr, it, items) {
    delete (*it);
  }
}

// copy all widget fields into variables
void Form::update() {
  if (controller->isRunning()) {
    Vector_each(WidgetDataPtr, it, items) {
      (*it)->transferData();
    }
  }
}

void Form::execute() {
  switch (code_peek()) {
  case kwTYPE_LINE:
  case kwTYPE_EOC:
  case kwTYPE_SEP:
    cmd = -1;
    var = 0;
    break;
  default:
    if (code_isvar()) {
      var = code_getvarptr();
      cmd = -1;
    } else {
      var_t variable;
      v_init(&variable);
      eval(&variable);
      cmd = v_getint(&variable);
      v_free(&variable);
      var = 0;

      // apply any configuration options
      switch (cmd) {
      case 1:
        kb_handle = true;
        return;
      default:
        break;
      }
    }
    break;
  };

  update();

  if (!cmd) {
    ui_reset();
  } else {
    // pump system messages until there is a widget callback
    mode = m_active;

    if (kb_handle) {
      dev_clrkb();
    }
    while (controller->isRunning() && mode == m_active) {
      controller->processEvents(-1, -1);
      if (kb_handle && keymap_kbhit()) {
        break;
      }
    }
    update();
  }
}

void Form::invoke(WidgetDataPtr widgetData) {
  mode = m_selected;

  if (var) {
    // array type cannot be used in program select statement
    if (widgetData->var->type == V_ARRAY) {
      v_zerostr(var);
    } else {
      // set the form variable from the widget var
      v_set(var, widgetData->var);
    }
  }
}

// WidgetData constructor
WidgetData::WidgetData(IFormWidget *widget, ControlType type, var_t *var) :
  widget(widget),
  type(type),
  var(var),
  is_group_radio(false) {
  orig.ptr = 0;
  orig.i = 0;
  setupWidget();
}

WidgetData::~WidgetData() {
  delete widget;
}

void WidgetData::setupWidget() {
  if (form == 0) {
    form = new Form();
  }
  form->add(this);
  widget->setListener(this);

  /*
  if (rect.width != -1) {
    widget->setWidth(rect.width);
  }

  if (rect.height != -1) {
    widget->setHeight(rect.height);
  }

  if (rect.left < 0) {
    rect.left = form->prev_x - rect.left;
  }

  if (rect.top < 0) {
    rect.top = form->prev_y - rect.top;
  }

  form->prev_x = rect.left + rect.width;
  form->prev_y = rect.top + rect.height;
  */
}

// update the smallbasic variable
void WidgetData::updateVarFlag() {
  switch (var->type) {
  case V_STR:
    orig.ptr = var->v.p.ptr;
    break;
  case V_ARRAY:
    orig.ptr = var->v.a.ptr;
    break;
  case V_INT:
    orig.i = var->v.i;
    break;
  default:
    orig.i = 0;
  }
}

// callback for the widget info called when the widget has been invoked
void WidgetData::buttonClicked(const char *action) {
  if (controller->isRunning()) {
    transferData();
    form->invoke(this);
  }
}

// set basic string variable to widget state when the variable has changed
bool WidgetData::updateGui() {
  ListModel *model;

  if (var->type == V_INT && var->v.i != orig.i) {
    // update list control with new int variable
    if (type == ctrl_listbox) {
      model = (ListModel *)widget->getList();
      model->setSelectedIndex(var->v.i);
      return true;
    }
    return false;
  }

  if (var->type == V_ARRAY && var->v.p.ptr != orig.ptr) {
    // update list control with new array variable
    String s;

    switch (type) {
    case ctrl_listbox:
      model = (ListModel *)widget->getList();
      model->clear();
      model->create(0, var);
      return true;

    case ctrl_label:
    case ctrl_text:
      array_to_string(s, var);
      widget->setText(s.c_str());
      break;

    default:
      return false;
    }
  }

  if (var->type == V_STR && orig.ptr != var->v.p.ptr) {
    // update list control with new string variable
    switch (type) {
    case ctrl_listbox:
      model = (ListModel *)widget->getList();
      if (strchr((const char *)var->v.p.ptr, '|')) {
        // create a new list of items
        model->clear();
        model->create((const char *)var->v.p.ptr, 0);
      } else {
        int selection = model->getIndex((const char *)var->v.p.ptr);
        if (selection != -1) {
          model->setSelectedIndex(selection);
        }
      }
      break;

    case ctrl_label:
    case ctrl_text:
    case ctrl_button:
      widget->setText((const char *)var->v.p.ptr);
      break;

    default:
      break;
    }
    return true;
  }
  return false;
}

// synchronise basic variable and widget state
void WidgetData::transferData() {
  const char *s;
  ListModel *model;

  if (updateGui()) {
    updateVarFlag();
    return;
  }

  // set widget state to basic variable
  switch (type) {
  case ctrl_text:
    s = widget->getText();
    if (s && s[0]) {
      v_setstr(var, s);
    } else {
      v_zerostr(var);
    }
    break;

  case ctrl_listbox:
    model = (ListModel *)widget->getList();
    const char *s = model->getTextAt(model->selectedIndex());
    if (s) {
      v_setstr(var, s);
    }
    break;

  case ctrl_button:
    // update the basic variable with the button label
    v_setstr(var, widget->getText());
    break;

  default:
    break;
  }

  // only update the gui when the variable is changed in basic code
  updateVarFlag();
}

C_LINKAGE_BEGIN 

// destroy the form
void ui_reset() {
  if (form != 0) {
    delete form;
    form = 0;
  }
}

// BUTTON x, y, w, h, variable, caption [,type]
//
void cmd_button() {
  var_int_t x, y, w, h;
  var_t *var = 0;
  char *caption = 0;
  char *type = 0;

  if (-1 != par_massget("IIIIPSs", &x, &y, &w, &h, &var, &caption, &type)) {
    IFormWidget *widget;
    if (type) {
      if (strcasecmp("button", type) == 0) {
        widget = controller->output->createButton(caption, x, y, w, h);
        new WidgetData(widget, ctrl_button, var);
      } else if (strcasecmp("label", type) == 0) {
        widget = controller->output->createLabel(caption, x, y, w, h);
        new WidgetData(widget, ctrl_label, var);
      } else if (strcasecmp("listbox", type) == 0 || 
                 strcasecmp("list", type) == 0) {
        ListModel *model = new ListModel(caption, var);
        widget = controller->output->createList(model, x, y, w, h);
        new WidgetData(widget, ctrl_listbox, var);
      } else {
        ui_reset();
        rt_raise("UI: UNKNOWN BUTTON TYPE: %s", type);
      }
    } else {
      widget = controller->output->createButton(caption, x, y, w, h);
      new WidgetData(widget, ctrl_button, var);
    }
  }

  pfree2(caption, type);
}

// TEXT x, y, w, h, variable
// When DOFORM returns the variable contains the user entered value
void cmd_text() {
  var_int_t x, y, w, h;
  var_t *var = 0;

  if (-1 != par_massget("IIIIP", &x, &y, &w, &h, &var)) {
    IFormWidget *widget = controller->output->createLineInput(NULL, 0, x, y, w, h);
    new WidgetData(widget, ctrl_text, var);
  }
}

// DOFORM [FLAG|VAR]
// Executes the form
void cmd_doform() {
  if (!form) {
    rt_raise("UI: FORM NOT READY");
  } else {
    form->execute();
  }
}

C_LINKAGE_END