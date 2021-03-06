/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/a11y/SelectionManager.h"

#include "DocAccessible-inl.h"
#include "nsAccessibilityService.h"
#include "nsAccUtils.h"
#include "nsCoreUtils.h"
#include "nsEventShell.h"

#include "nsIAccessibleTypes.h"
#include "nsIDOMDocument.h"
#include "nsIPresShell.h"
#include "nsISelectionPrivate.h"
#include "mozilla/Selection.h"
#include "mozilla/dom/Element.h"

using namespace mozilla;
using namespace mozilla::a11y;

void
SelectionManager::ClearControlSelectionListener()
{
  if (!mCurrCtrlFrame)
    return;

  const nsFrameSelection* frameSel = mCurrCtrlFrame->GetConstFrameSelection();
  NS_ASSERTION(frameSel, "No frame selection for the element!");

  mCurrCtrlFrame = nullptr;
  if (!frameSel)
    return;

  // Remove 'this' registered as selection listener for the normal selection.
  Selection* normalSel =
    frameSel->GetSelection(nsISelectionController::SELECTION_NORMAL);
  normalSel->RemoveSelectionListener(this);

  // Remove 'this' registered as selection listener for the spellcheck
  // selection.
  Selection* spellSel =
    frameSel->GetSelection(nsISelectionController::SELECTION_SPELLCHECK);
  spellSel->RemoveSelectionListener(this);
}

void
SelectionManager::SetControlSelectionListener(dom::Element* aFocusedElm)
{
  // When focus moves such that the caret is part of a new frame selection
  // this removes the old selection listener and attaches a new one for
  // the current focus.
  ClearControlSelectionListener();

  mCurrCtrlFrame = aFocusedElm->GetPrimaryFrame();
  if (!mCurrCtrlFrame)
    return;

  const nsFrameSelection* frameSel = mCurrCtrlFrame->GetConstFrameSelection();
  NS_ASSERTION(frameSel, "No frame selection for focused element!");
  if (!frameSel)
    return;

  // Register 'this' as selection listener for the normal selection.
  Selection* normalSel =
    frameSel->GetSelection(nsISelectionController::SELECTION_NORMAL);
  normalSel->AddSelectionListener(this);

  // Register 'this' as selection listener for the spell check selection.
  Selection* spellSel =
    frameSel->GetSelection(nsISelectionController::SELECTION_SPELLCHECK);
  spellSel->AddSelectionListener(this);
}

void
SelectionManager::AddDocSelectionListener(nsIPresShell* aPresShell)
{
  const nsFrameSelection* frameSel = aPresShell->ConstFrameSelection();

  // Register 'this' as selection listener for the normal selection.
  Selection* normalSel =
    frameSel->GetSelection(nsISelectionController::SELECTION_NORMAL);
  normalSel->AddSelectionListener(this);

  // Register 'this' as selection listener for the spell check selection.
  Selection* spellSel =
    frameSel->GetSelection(nsISelectionController::SELECTION_SPELLCHECK);
  spellSel->AddSelectionListener(this);
}

void
SelectionManager::RemoveDocSelectionListener(nsIPresShell* aPresShell)
{
  const nsFrameSelection* frameSel = aPresShell->ConstFrameSelection();

  // Remove 'this' registered as selection listener for the normal selection.
  Selection* normalSel =
    frameSel->GetSelection(nsISelectionController::SELECTION_NORMAL);
  normalSel->RemoveSelectionListener(this);

  // Remove 'this' registered as selection listener for the spellcheck
  // selection.
  Selection* spellSel =
    frameSel->GetSelection(nsISelectionController::SELECTION_SPELLCHECK);
  spellSel->RemoveSelectionListener(this);
}

void
SelectionManager::ProcessTextSelChangeEvent(AccEvent* aEvent)
{
  AccTextSelChangeEvent* event = downcast_accEvent(aEvent);
  Selection* sel = static_cast<Selection*>(event->mSel.get());

  // Fire selection change event if it's not pure caret-move selection change.
  if (sel->GetRangeCount() != 1 || !sel->IsCollapsed())
    nsEventShell::FireEvent(aEvent);

  // Fire caret move event if there's a caret in the selection.
  nsINode* caretCntrNode =
    nsCoreUtils::GetDOMNodeFromDOMPoint(sel->GetFocusNode(),
                                        sel->GetFocusOffset());
  if (!caretCntrNode)
    return;

  HyperTextAccessible* caretCntr = nsAccUtils::GetTextContainer(caretCntrNode);
  NS_ASSERTION(caretCntr,
               "No text container for focus while there's one for common ancestor?!");
  if (!caretCntr)
    return;

  int32_t caretOffset = caretCntr->CaretOffset();
  if (caretOffset != -1) {
    nsRefPtr<AccCaretMoveEvent> caretMoveEvent =
      new AccCaretMoveEvent(caretCntr, caretOffset, aEvent->FromUserInput());
    nsEventShell::FireEvent(caretMoveEvent);
  }
}

NS_IMETHODIMP
SelectionManager::NotifySelectionChanged(nsIDOMDocument* aDOMDocument,
                                         nsISelection* aSelection,
                                         int16_t aReason)
{
  NS_ENSURE_ARG(aDOMDocument);

  nsCOMPtr<nsIDocument> documentNode(do_QueryInterface(aDOMDocument));
  DocAccessible* document = GetAccService()->GetDocAccessible(documentNode);

#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eSelection))
    logging::SelChange(aSelection, document);
#endif

  // Don't fire events until document is loaded.
  if (document && document->IsContentLoaded()) {
    // Selection manager has longer lifetime than any document accessible,
    // so that we are guaranteed that the notification is processed before
    // the selection manager is destroyed.
    document->HandleNotification<SelectionManager, nsISelection>
      (this, &SelectionManager::ProcessSelectionChanged, aSelection);
  }

  return NS_OK;
}

void
SelectionManager::ProcessSelectionChanged(nsISelection* aSelection)
{
  Selection* selection = static_cast<Selection*>(aSelection);
  if (!selection->GetPresShell())
    return;

  const nsRange* range = selection->GetAnchorFocusRange();
  nsINode* cntrNode = nullptr;
  if (range)
    cntrNode = range->GetCommonAncestor();
  if (!cntrNode) {
    cntrNode = selection->GetFrameSelection()->GetAncestorLimiter();
    if (!cntrNode) {
      cntrNode = selection->GetPresShell()->GetDocument();
      NS_ASSERTION(selection->GetPresShell()->ConstFrameSelection() == selection->GetFrameSelection(),
                   "Wrong selection container was used!");
    }
  }

  HyperTextAccessible* text = nsAccUtils::GetTextContainer(cntrNode);
  if (!text) {
    NS_NOTREACHED("We must reach document accessible implementing text interface!");
    return;
  }

  if (selection->GetType() == nsISelectionController::SELECTION_NORMAL) {
    nsRefPtr<AccEvent> event = new AccTextSelChangeEvent(text, aSelection);
    text->Document()->FireDelayedEvent(event);

  } else if (selection->GetType() == nsISelectionController::SELECTION_SPELLCHECK) {
    // XXX: fire an event for container accessible of the focus/anchor range
    // of the spelcheck selection.
    text->Document()->FireDelayedEvent(nsIAccessibleEvent::EVENT_TEXT_ATTRIBUTE_CHANGED,
                                       text);
  }
}
