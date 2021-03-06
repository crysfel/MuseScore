//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2002-2011 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "globals.h"
#include "scoreview.h"
#include "preferences.h"
#include "musescore.h"
#include "textpalette.h"
#include "texttools.h"
#include "inspector/inspector.h"

#include "libmscore/barline.h"
#include "libmscore/utils.h"
#include "libmscore/segment.h"
#include "libmscore/score.h"
#include "libmscore/undo.h"
#include "libmscore/text.h"
#include "libmscore/spanner.h"
#include "libmscore/measure.h"
#include "libmscore/textframe.h"

namespace Ms {

//---------------------------------------------------------
//   updateGrips
//    if (curGrip == -1) then initialize to element
//    default grip
//---------------------------------------------------------

void ScoreView::updateGrips()
      {
      if (!editData.element)
            return;

      double dx = 1.5 / _matrix.m11();
      double dy = 1.5 / _matrix.m22();

      for (const QRectF& r : editData.grip)
            score()->addRefresh(r.adjusted(-dx, -dy, dx, dy));

      qreal w   = 8.0 / _matrix.m11();
      qreal h   = 8.0 / _matrix.m22();
      QRectF r(-w*.5, -h*.5, w, h);

      editData.grip.resize(editData.grips);

      if (editData.grips) {
            for (QRectF& gr : editData.grip)
                  gr = r;
            editData.element->updateGrips(editData);

            // updateGrips returns grips in page coordinates,
            // transform to view coordinates:

            Element* page = editData.element;
            while (page->parent())
                  page = page->parent();
            QPointF pageOffset(page->pos());

            for (QRectF& grip : editData.grip) {
                  grip.translate(pageOffset);
                  score()->addRefresh(grip.adjusted(-dx, -dy, dx, dy));
                  }

            QPointF anchor = editData.element->gripAnchor(editData.curGrip);
            if (!anchor.isNull())
                  setDropAnchor(QLineF(anchor + pageOffset, editData.grip[int(editData.curGrip)].center()));
            else
                  setDropTarget(0); // this also resets dropAnchor
            }
      score()->addRefresh(editData.element->canvasBoundingRect());
      }

//---------------------------------------------------------
//   startEdit
//---------------------------------------------------------

void ScoreView::startEditMode(Element* e)
      {
      if (!e || !e->isEditable()) {
            qDebug("The element cannot be edited");
            return;
            }
      editData.element = e;
      changeState(ViewState::EDIT);
      }

//---------------------------------------------------------
//   startEdit
//---------------------------------------------------------

void ScoreView::startEdit(Element* element, Grip startGrip)
      {
      if (!element || !element->isEditable()) {
            qDebug("The element cannot be edited");
            return;
            }
      editData.element = element;
      changeState(ViewState::EDIT);
      if (startGrip != Grip::NO_GRIP)
            editData.curGrip = startGrip;
      }

//---------------------------------------------------------
//   startEdit
//    enter state EDIT
//---------------------------------------------------------

void ScoreView::startEdit()
      {
      if (editData.element->isTBox())
            editData.element = toTBox(editData.element)->text();
      setFocus();
      editData.grips   = 0;
      editData.curGrip = Grip(0);
      editData.clearData();

      editData.element->startEdit(editData);
      updateGrips();
      _score->update();
      setCursor(QCursor(Qt::ArrowCursor));
      }

//---------------------------------------------------------
//   endEdit
//---------------------------------------------------------

void ScoreView::endEdit()
      {
      setDropTarget(0);
      if (!editData.element)
            return;
      _score->addRefresh(editData.element->canvasBoundingRect());
      for (int i = 0; i < editData.grips; ++i)
            score()->addRefresh(editData.grip[i]);
      editData.element->endEdit(editData);

      _score->addRefresh(editData.element->canvasBoundingRect());

      ElementType tp = editData.element->type();
      if (tp == ElementType::LYRICS)
            lyricsEndEdit();
      else if (tp == ElementType::HARMONY)
            harmonyEndEdit();
      else if (tp == ElementType::FIGURED_BASS)
            figuredBassEndEdit();
      else if (editData.element->isText()) {
            Text* text = toText(editData.element);
            // remove text if empty
            // dont do this for TBOX
            if (text->empty() && text->parent() && text->parent()->type() != ElementType::TBOX)
                  _score->undoRemoveElement(text);
            }
#if 0
      if (dragElement && (dragElement != editData.element)) {
            curElement = dragElement;
            _score->select(curElement);
            _score->update();
            }
#endif
      editData.clearData();
      mscore->updateInspector();
      }

//---------------------------------------------------------
//   editData.elementDragTransition
//    start dragEdit
//---------------------------------------------------------

bool ScoreView::editElementDragTransition(QMouseEvent* ev)
      {
      score()->startCmd();
      editData.element->startEditDrag(editData);

      if (editData.element->isText()) {
            qreal margin = editData.element->spatium();
            QRectF r = editData.element->pageBoundingRect().adjusted(-margin, -margin, margin, margin);
            if (r.contains(editData.pos)) {
                  if (editData.element->shape().translated(editData.element->pagePos()).contains(editData.pos)) {
                        if (editData.element->mousePress(editData, ev)) {
                              _score->addRefresh(editData.element->canvasBoundingRect());
                              _score->update();
                              }
                        }
                  return true;
                  }
            return false;
            }
      return true;
      }

//---------------------------------------------------------
//   doDragEdit
//---------------------------------------------------------

void ScoreView::doDragEdit(QMouseEvent* ev)
      {
      editData.lastPos   = editData.pos;
      editData.pos       = toLogical(ev->pos());
      editData.modifiers = qApp->keyboardModifiers();

      if (!editData.element->isBarLine()) {
            // on other elements, BOTH Ctrl (vert. constrain) and Shift (horiz. constrain) modifiers = NO constrain
            if (qApp->keyboardModifiers() == Qt::ShiftModifier)
                  editData.pos.setX(editData.lastPos.x());
            if (qApp->keyboardModifiers() == Qt::ControlModifier)
                  editData.pos.setY(editData.lastPos.y());
            }
      editData.delta = editData.pos - editData.lastPos;
      score()->addRefresh(editData.element->canvasBoundingRect());

      if (editData.element->isText()) {
            if (editData.element->shape().translated(editData.element->pagePos()).contains(editData.pos)) {
                  qDebug("in");
                  toText(editData.element)->dragTo(editData);
                  }
            else {
                  qDebug("out");
                  editData.hRaster = false;
                  editData.vRaster = false;
                  editData.element->editDrag(editData);
                  updateGrips();
                  }
            }
      else {
            editData.hRaster = false;
            editData.vRaster = false;
            editData.element->editDrag(editData);
            updateGrips();
            }
      QRectF r(editData.element->canvasBoundingRect());
      _score->addRefresh(r);
      _score->update();
      }

//---------------------------------------------------------
//   endDragEdit
//---------------------------------------------------------

void ScoreView::endDragEdit()
      {
      _score->addRefresh(editData.element->canvasBoundingRect());
      editData.element->endEditDrag(editData);
      score()->endCmd();
      setDropTarget(0);
      updateGrips();
      _score->rebuildBspTree();
      _score->addRefresh(editData.element->canvasBoundingRect());
      _score->update();
      }
}

