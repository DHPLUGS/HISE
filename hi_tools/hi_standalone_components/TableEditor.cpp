/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   Commercial licenses for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licensing:
*
*   http://www.hise.audio/
*
*   HISE is based on the JUCE library,
*   which must be separately licensed for closed source applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

namespace hise { using namespace juce;

//==============================================================================
TableEditor::TableEditor(UndoManager* undoManager_, Table *tableToBeEdited):
	domainRange(Range<int>()),
	currentType(DomainType::originalSize),
	displayIndex(0.0f)
{
	setUseFlatDesign(false);

	if (tableToBeEdited != nullptr)
		setEditedTable(tableToBeEdited);
	else
		setEditedTable(&dummyTable);

	setSpecialLookAndFeel(&defaultLaf, false);

    // MUST BE SET!
	jassert(editedTable != nullptr);

	addAndMakeVisible(ruler = new Ruler());

	fontToUse = GLOBAL_BOLD_FONT();
	
	ruler->setAlwaysOnTop(true);

	// APEX prototype: the ruler listens to the display link; it only does work while the
	// edited table publishes a playhead state (see Ruler::onDisplayFrame).
	DisplayLinkClock::getInstance()->addListener(ruler);

	setColour(ColourIds::bgColour, Colours::transparentBlack);
	setColour(ColourIds::fillColour, Colours::white.withAlpha(0.2f));
	setColour(ColourIds::lineColour, Colours::white);
	setColour(ColourIds::rulerColour, Colours::white);
	setColour(ColourIds::overlayTextId, Colour(0xDD000000));
	setColour(ColourIds::overlayBgColour, Colour(0xBBffffff));

	popupFunction = BIND_MEMBER_FUNCTION_2(TableEditor::getPopupString);

	if (undoManager_ != nullptr)
	{
		getEditedTable()->setUndoManager(undoManager_);
	}
}

TableEditor::~TableEditor()
{
	setEditedTable(nullptr);

	closeTouchOverlay();
}

void TableEditor::refreshGraph()
{	
	invalidatePaintCache();

	auto closePath = getTableLookAndFeel()->shouldClosePath() && dragProperties.closePath;

	if(editedTable.get() != nullptr) 
		editedTable->createPath(dragPath, closePath, dragProperties.closePath);

	auto a = getTableArea();
	dragPath.scaleToFit(a.getX(), a.getY(), a.getWidth(), a.getHeight(), false);

	mid_points.clear();

	if (dragProperties.midPointSize > 0 && (drag_points.size() > 2 || !dragProperties.syncStartEnd))
	{
		auto width = (float)a.getWidth();
		float w = (float)dragProperties.midPointSize;

		for (int i = 0; i < drag_points.size() - 1; i++)
		{
			auto tp = drag_points[i]->getGraphPoint();
			auto np = drag_points[i + 1]->getGraphPoint();

			auto dist = roundToInt((np.x - tp.x) * width);

			if(dist < dragProperties.midPointSize)
				continue;

			auto midX = a.getX() + (tp.x + 0.5f * (np.x - tp.x)) * width;
			auto y = flex_ahdsr_base::Helpers::getYAt(dragPath, midX);

			midX -= w * 0.5f;
			y -= w * 0.5f;

			Rectangle<float> a(midX, y, w, w);

			mid_points.push_back({ i, a });
		}
	}

	needsRepaint = true;
	repaint();
}

float TableEditor::snapXValueToGrid(float x) const
{
	auto a = getTableArea();

	if (snapValues.size() == 0 || !dragProperties.snapToGrid)
		return x;

	auto normalizedX = (x - a.getX()) / a.getWidth();

	auto snapRangeHalfWidth = dragProperties.snapWidth / a.getWidth();

	for (int i = 0; i < snapValues.size(); i++)
	{
		auto snapValue = (float)snapValues[i];
		auto snapRange = Range<float>(snapValue - snapRangeHalfWidth, snapValue + snapRangeHalfWidth);

		if (snapRange.contains(normalizedX))
			return a.getX() + snapValue * a.getWidth();
	}

	return x;
}

void TableEditor::mouseWheelMove(const MouseEvent &e, const MouseWheelDetails &wheel)
{
	if(dragProperties.useMouseWheelForCurve)
	{
		MouseEvent parentEvent = e.getEventRelativeTo(this);
		int x = parentEvent.getMouseDownPosition().getX();
		int y = parentEvent.getMouseDownPosition().getY();

		DragPoint* dp = getNextPointFor(x);

		int thisIndex = drag_points.indexOf(dp);

		int prevIndex = thisIndex - 1;

		DragPoint* pp = drag_points[prevIndex];

		bool useEvent = dp != nullptr;

		useEvent &= e.mods == scrollModifiers;

		if (useEvent)
		{
			if (getUndoManager(thisIndex != lastEditedPointIndex))
			{
				lastEditedPointIndex = thisIndex;
			}

			updateCurve(x, y, wheel.deltaY, true);

			if (pp != nullptr)
			{
				auto curveValue = dp->getCurve();

				auto ar = Rectangle<int>(pp->getPos().toInt(), dp->getPos().toInt());
				auto middle = ar.getCentre();

				ScopedLock sl(editListeners.getLock());

				for (auto l : editListeners)
				{
					if (l.get() != nullptr)
						l->curveChanged(middle, curveValue);
				}
			}

			return;
		}
	}

	getParentComponent()->mouseWheelMove(e, wheel);
};


void TableEditor::updateCurve(int x, int y, float newCurveValue, bool useUndoManager)
{
	auto dp = getNextPointFor(x);

	auto pp = getPrevPointFor(x);

	if (dp == nullptr)
		return;

	if (auto um = getUndoManager(useUndoManager))
	{
		um->perform(new TableAction(this, TableAction::Curve, -1, x, y, newCurveValue, x, y, -1.0f * newCurveValue));
	}
	else
	{
		if (pp != nullptr && pp->getGraphPoint().y > dp->getGraphPoint().y)
			newCurveValue *= -1.0f;

		dp->updateCurve(newCurveValue);
		updateTable(true);
		refreshGraph();
	}
}






juce::String TableEditor::getPopupString(float x, float y)
{
	auto xName = editedTable->getXValueText(x);
	auto yName = editedTable->getYValueText(y);

	return xName + " | " + yName;
}

juce::Rectangle<int> TableEditor::getPointAreaBetweenMouse() const
{
	return pointAreaBetweenMouse;
}



void TableEditor::addDragPoint(int x, int y, float curve, bool isStart/*=false*/, bool isEnd/*=false*/, bool useUndoManager/*=false*/)
{
	if (auto um = getUndoManager(useUndoManager))
	{
		um->perform(new TableAction(this, TableAction::Add, -1, x, y, curve, -1, -1, -1.0f));
	}
	else
	{
		DragPoint *dp = new DragPoint(*this, isStart, isEnd);

		dp->setCurve(curve);

		dp->setTableEditorSize(getTableArea());
		dp->setPosPixel(Point<float>((float)x, (float)y));
		addAndMakeVisible(dp);

#if USE_BACKEND
		if (isStart)
			DocumentWindowWithEmbeddedPopupMenu::setSubComponentTargetId(dp, "start");
		else if (isEnd)
			DocumentWindowWithEmbeddedPopupMenu::setSubComponentTargetId(dp, "end");
		else
			DocumentWindowWithEmbeddedPopupMenu::setSubComponentTargetId(dp, "point_" + String(drag_points.size()));
#endif

		DragPointComparator comparator;

		drag_points.addSorted(comparator, dp);

		if (!(isEnd || isStart)) 
			currently_dragged_point = nullptr; // fix #59
	}
}

void TableEditor::createDragPoints()
{
	
	//jassert(getWidth() != 0);

	drag_points.clear();

	if(editedTable.get() != nullptr) 
	{
        auto list = editedTable->getCopyOfGraphPoints();
        
        jassert(list.size() >= 2);
        
		addNormalizedDragPoint(list.getFirst(), true, false);
        
		for(int i = 1; i < list.size() - 1; ++i)
            addNormalizedDragPoint(list[i], false, false);
        
		addNormalizedDragPoint(list.getLast(), false, true);
	}

	
};

void TableEditor::setDisplayedIndex(float newIndex)
{
	lastIndex = newIndex;

	if (ruler != nullptr)
		ruler->setIndex(newIndex);
}

void TableEditor::setEdge(float f, bool setLeftEdge)
{
	auto a = getTableArea();

	if(setLeftEdge)	drag_points.getFirst()->changePos(Point<float>(0.0f, a.getY() + (1.0f - f) * a.getHeight()));
	else drag_points.getLast()->changePos(Point<float>((float)getWidth(), a.getY() + (1.0f - f) * a.getHeight()));

	updateTable(true);
	refreshGraph();
}



void TableEditor::paint (Graphics& g)
{
	if (editedTable.get() == nullptr)
	{
		g.setFont(GLOBAL_BOLD_FONT());
		g.setColour(Colours::white.withAlpha(0.5f));
		g.drawText("No table", getLocalBounds(), Justification::centred);
		return;
	}

	if (ruler != nullptr && ruler->isPlayheadActive() && getWidth() > 0 && getHeight() > 0)
	{
		// APEX prototype: playhead mode. A repaint whose clip lies inside the strips the
		// ruler asked for is served from the cache; anything larger re-renders it.
		const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
		const int cw = jmax(1, roundToInt((float)getWidth() * scale));
		const int ch = jmax(1, roundToInt((float)getHeight() * scale));

		const bool cacheFits = paintCache.isValid() && paintCache.getWidth() == cw && paintCache.getHeight() == ch;
		const bool stripRepaint = paintCacheValid && cacheFits && !playheadClip.isEmpty() && playheadClip.contains(g.getClipBounds());

		if (!stripRepaint)
		{
			if (!cacheFits)
				paintCache = Image(Image::ARGB, cw, ch, true);
			else
				paintCache.clear(paintCache.getBounds());

			Graphics ig(paintCache);
			ig.addTransform(AffineTransform::scale(scale));
			renderStaticContent(ig);
			paintCacheValid = true;
		}

		g.setImageResamplingQuality(Graphics::lowResamplingQuality);
		g.drawImageTransformed(paintCache, AffineTransform::scale(1.0f / scale), false);
	}
	else
	{
		if (paintCache.isValid())
		{
			paintCache = Image();
			paintCacheValid = false;
		}

		renderStaticContent(g);
	}
	
    if (currently_dragged_point != nullptr)
    {
		auto a = getTableArea();

        DragPoint *dp = currently_dragged_point;
        
		auto text = popupFunction(dp->getGraphPoint().x, dp->getGraphPoint().y);
		int boxWidth = (int)fontToUse.getStringWidth(text) + 10;
		int boxHeight = (int)fontToUse.getHeight() + 10;

		if(boxWidth < a.getWidth())
		{
			int x_ = jlimit<int>((int)a.getX(), (int)a.getRight() - boxWidth, (int)dp->getPos().x - boxWidth / 2);
			int y_ = jlimit<int>((int)a.getY(), (int)a.getBottom() - boxHeight, (int)dp->getPos().y - 20);

			Rectangle<int> area(x_, y_, boxWidth, boxHeight);

			if (auto l = getTableLookAndFeel())
				l->drawTableValueLabel(g, *this, fontToUse, text, area);
		}
    }
    
    g.setOpacity(isEnabled() ? 1.0f : 0.2f);
 
}

void TableEditor::Ruler::paint(Graphics &g)
{
	auto te = findParentComponentOfClass<TableEditor>();

	if (te == nullptr)
		return;

	if (playheadActive)
	{
		// APEX prototype: drawn natively so that a per-frame playhead never goes through a
		// scripted look and feel. Colour is rulerColour (ScriptTable: textColour).
		if (!playheadVisible)
			return;

		const auto area = te->getTableArea();
		const float x = (float)(area.getX() + playheadPos * area.getWidth());

		g.setColour(te->findColour(TableEditor::ColourIds::rulerColour));
		g.drawLine(Line<float>(x, area.getY(), x, area.getBottom()), jmax(1.0f, te->lineThickness * 0.5f));
		return;
	}

	if (auto l = te->getTableLookAndFeel())
		l->drawTableRuler(g, *te, te->getTableArea(), te->lineThickness, value);
}

void TableEditor::resized()
{
	if (editedTable.get() != nullptr)
	{
		this->removeMouseListener(this);
		this->addMouseListener(this, true);

		ruler->setBounds(0, 0, getWidth(), getHeight());
		invalidatePaintCache();

		if (getHeight() > 0 && getWidth() > 0)
		{
			snapshot = Image(Image::ARGB, getWidth(), getHeight(), true);

			createDragPoints();
			refreshGraph();
		}
	}
}



void TableEditor::graphHasChanged(int point)
{
	SafeAsyncCall::call<TableEditor>(*this, [](TableEditor& t) 
	{	
		if (t.currently_dragged_point == nullptr)
		{
			t.createDragPoints();
			t.refreshGraph();
		}
	});
}

void TableEditor::setDomain(DomainType newDomainType, Range<int> newRange)
{
	currentType = newDomainType;
	if( currentType == DomainType::scaled ) domainRange = newRange;
	else jassert ( newRange.isEmpty() );
};

void TableEditor::setSnapValues(var snapArray)
{
	if (auto ar = snapArray.getArray())
	{
		snapValues.clear();

		for (const auto& v : *ar)
		{
			DBG((float)v);
			snapValues.add((float)v);
		}

		// Refresh so a grid drawn in the look and feel updates immediately.
		repaint();
	}
}

void TableEditor::mouseDown(const MouseEvent &e)
{
	CHECK_MIDDLE_MOUSE_DOWN(e);

	if (!isEnabled()) return;

	if (e.mods.isAltDown())
		return;

	grabCopyAndPasteFocus();

	MouseEvent parentEvent = e.getEventRelativeTo(this);
	int x = parentEvent.getMouseDownPosition().getX();
	int y = parentEvent.getMouseDownPosition().getY();

	// Shape draw mode: a left click stamps the active shape into the grid cell under
	// the mouse (right / cmd click still falls through to delete / curve editing).
	if (dragProperties.drawShape != 0 && e.mods.isLeftButtonDown() && !e.mods.isCommandDown())
	{
		auto a = getTableArea();
		lastStampedCell = -1;
		lastStampedAmp = -1.0f;
		const float amp = jlimit(0.0f, 1.0f, 1.0f - ((float)y - a.getY()) / a.getHeight());
		stampDrawShapeAt((float)(x - a.getX()) / a.getWidth(), amp);
		return;
	}

	auto midPointIndex = getDraggedMidPointIndex(parentEvent);

	if(midPointIndex != -1)
	{
		lastRightDragValue = (float)parentEvent.getPosition().getY();
		draggedMidPointIndex = midPointIndex;
		needsRepaint = true;
		repaint();
		return;
	}
	else
	{
		draggedMidPointIndex = -1;
	}

	x = (int)snapXValueToGrid((float)x);

	DragPoint *dp = this->getPointUnder(x, y);

	lastEditedPointIndex = drag_points.indexOf(dp);

	if(e.mods.isRightButtonDown() || e.mods.isCommandDown())
	{
		if (dp != nullptr)
		{
			removeDragPoint(dp);

			if (editedTable.get() != nullptr)
				editedTable->sendGraphUpdateMessage();
		}

		lastRightDragValue = (float)e.getPosition().getY();
	}
	else
	{
		if (dp != nullptr)
		{
			currently_dragged_point = dp;

			showTouchOverlay();

			ScopedLock sl(editListeners.getLock());

			for (auto l : editListeners)
			{
				if (l.get() != nullptr)
				{
					l->pointDragStarted(dp->getPosition(), dp->getGraphPoint().x, dp->getGraphPoint().y);
				}
			}
		}
		else
		{
			pointAreaBetweenMouse = {};
			x = (int)snapXValueToGrid((float)x);

			addDragPoint(x, y, 0.5f, false, false, true);
		}
	}

	updateTable(false);
	refreshGraph();

	needsRepaint = true;
	repaint();
}


void TableEditor::removeDragPoint(DragPoint * dp, bool useUndoManager)
{
	if (!dp->isStartOrEnd())
	{
		if (auto um = getUndoManager(useUndoManager))
		{
			int x = dp->getBoundsInParent().getCentreX();
			int y = dp->getBoundsInParent().getCentreY();

			um->perform(new TableAction(this, TableAction::Delete, -1, -1, -1, -1, x, y, dp->getCurve()));
		}
		else
		{
			drag_points.remove(drag_points.indexOf(dp));

			updateTable(true);
			refreshGraph();

			needsRepaint = true;
			repaint();
		}


	}
}

void TableEditor::mouseDoubleClick(const MouseEvent& e)
{
	if (!isEnabled()) return;

	MouseEvent parentEvent = e.getEventRelativeTo(this);
	int x = parentEvent.getMouseDownPosition().getX();
	int y = parentEvent.getMouseDownPosition().getY();

	auto midPointIndex = getDraggedMidPointIndex(parentEvent);

	if(midPointIndex != -1)
	{
		// reset to middle
		if(isPositiveAndBelow(midPointIndex + 1, drag_points.size()))
		{
			drag_points[midPointIndex +1]->setCurve(0.5f);
		}
	}
	else
	{
		Component* clickedComponent = this->getComponentAt(x, y);

		if (clickedComponent != this)
		{
			DragPoint* dp = this->getPointUnder(x, y);
			if (!dp->isStartOrEnd())
			{

				drag_points.remove(drag_points.indexOf(dp));

				updateTable(true);
			}
		}
	}
	
	updateTable(false);
	refreshGraph();

	needsRepaint = true;
	repaint();
}

void TableEditor::mouseUp(const MouseEvent& e)
{
	CHECK_MIDDLE_MOUSE_UP(e);

	if (!isEnabled()) return;

	if (e.mods.isAltDown())
		return;

	closeTouchOverlay();

	draggedMidPointIndex = -1;
	currently_dragged_point = nullptr;
	updateTable(true);

	needsRepaint = true;
	repaint();

	ScopedLock sl(editListeners.getLock());

	for (auto l : editListeners)
	{
		if (l.get() != nullptr)
			l->pointDragEnded();
	}
}

void TableEditor::mouseDrag(const MouseEvent &e)
{
	CHECK_MIDDLE_MOUSE_DRAG(e);

	if (!isEnabled()) return;

	if (e.mods.isAltDown())
		return;

	MouseEvent parentEvent = e.getEventRelativeTo(this);

	float x = parentEvent.position.getX();
	float y = parentEvent.position.getY();

	// Shape draw mode: dragging paints the active shape across each grid cell the
	// cursor crosses, and the cursor height sets each cell's amplitude live (re-stamps
	// the current cell as the height changes, paints new cells as x crosses them).
	if (dragProperties.drawShape != 0 && e.mods.isLeftButtonDown())
	{
		auto a = getTableArea();
		const float amp = jlimit(0.0f, 1.0f, 1.0f - (y - a.getY()) / a.getHeight());
		stampDrawShapeAt((x - a.getX()) / a.getWidth(), amp);
		return;
	}

	if (draggedMidPointIndex != -1)
	{
		auto mx = parentEvent.getMouseDownX();
		auto thisPos = (float)parentEvent.getPosition().getY();
		auto delta = thisPos - lastRightDragValue;

		lastRightDragValue = thisPos;

		auto y1 = drag_points[draggedMidPointIndex]->getGraphPoint().y;
		auto y2 = drag_points[draggedMidPointIndex+1]->getGraphPoint().y;

		// A flat segment (e.g. a drawn Column / plateau) has equal endpoints, so there
		// is nothing to bend. Without this guard the divide below is by ~zero, which
		// slams the curve to its limit and makes the handle jump to the top.
		const float dy = hmath::abs(y2 - y1);

		if (dy < 0.0001f)
			return;

		delta /= dy;

		delta /= (float)getHeight();
		delta *= -6.0f;
		updateCurve(mx, (int)y, delta, true);
		return;
	}

	auto a = getTableArea();

	if (currently_dragged_point == nullptr)
	{
		if (e.mods.isRightButtonDown())
		{
			auto thisPos = (float)parentEvent.getPosition().getY();

			auto delta = thisPos - lastRightDragValue;

			lastRightDragValue = thisPos;

			delta /= (float)getHeight();

			delta *= -4.0f;

			updateCurve((int)x, (int)y, delta, true);
		}

		return;
	}

	if(!currently_dragged_point->canBeModified(dragProperties))
		return;

	if (parentEvent.mods.isShiftDown())
		x = (float)parentEvent.getMouseDownPosition().getX();

	if(dragProperties.allowSwap)
	{
		x = jmin(x, a.getWidth() - 1.0f);
		x = jmax(x, 1.0f);
	}
	else
	{
		auto dragIndex = drag_points.indexOf(currently_dragged_point);

		auto prevPoint = dragIndex > 1 ? drag_points[dragIndex - 1] : nullptr;
		auto nextPoint = dragIndex < (drag_points.size() - 2) ? drag_points[dragIndex+1] : nullptr;

		auto prevX = prevPoint != nullptr ? prevPoint->getPos().getX() : a.getX();
		auto nextX = nextPoint != nullptr ? nextPoint->getPos().getX() : a.getRight();

		x = jlimit(prevX, nextX, x);
	}


	y = jlimit(a.getY(), a.getBottom(), y);

	//y = jmin(y, (int)a.getHeight());
	//y = jmax(y, 0);

	x = snapXValueToGrid(x);

	auto index = drag_points.indexOf(currently_dragged_point);

	changePointPosition(index, x, y, true);

	DragPoint* other = nullptr;

	if(currently_dragged_point->isStartOrEnd() && dragProperties.syncStartEnd)
	{
		other = index == 0 ? drag_points.getLast() : drag_points.getFirst();
		auto otherIndex = drag_points.indexOf(other);
		auto x = other->getPos().getX();

		changePointPosition(otherIndex, x, y, true);
	}

	ScopedLock sl(editListeners.getLock());

	for (auto l : editListeners)
	{
		if (l.get() != nullptr)
		{
			if(other != nullptr)
				l->pointDragged(other->getPosition(), other->getGraphPoint().x, other->getGraphPoint().y);

			l->pointDragged(currently_dragged_point->getPosition(), currently_dragged_point->getGraphPoint().x, currently_dragged_point->getGraphPoint().y);
		}
	}
};

void TableEditor::mouseMove(const MouseEvent& e)
{
	hoveredMidPointIndex = getDraggedMidPointIndex(e);

	setMouseCursor(hoveredMidPointIndex != -1 ? MouseCursor(dragProperties.midPointCursor) : MouseCursor::NormalCursor);

	if (e.eventComponent != this)
	{
		pointAreaBetweenMouse = {};
	}
	else
	{
		auto pp = getPrevPointFor(e.getPosition().getX());
		auto dp = getNextPointFor(e.getPosition().getX());

		if (pp != nullptr && dp != nullptr)
		{
			pointAreaBetweenMouse = Rectangle<int>(pp->getPos().toInt(), dp->getPos().toInt());
			pointAreaBetweenMouse = pointAreaBetweenMouse.withY(0).withHeight(getHeight());
		}
		else
		{
			pointAreaBetweenMouse = {};
		}
	}

	repaint();
}

void TableEditor::mouseExit(const MouseEvent& e)
{
	pointAreaBetweenMouse = {};
	repaint();
}

void TableEditor::showTouchOverlay()
{
#if HISE_IOS
	auto mainWindow = getTopLevelComponent();
	mainWindow->addAndMakeVisible(touchOverlay = new TouchOverlay(currently_dragged_point));
	updateTouchOverlayPosition();
#endif
}



void TableEditor::closeTouchOverlay()
{
#if HISE_IOS
	if (touchOverlay != nullptr)
	{
		auto mainWindow = getTopLevelComponent();

		if (mainWindow != nullptr)
		{
			mainWindow->removeChildComponent(touchOverlay);
			touchOverlay = nullptr;
		}
	}
#endif
}

void TableEditor::updateTouchOverlayPosition()
{
#if HISE_IOS
	auto mw = getTopLevelComponent();
	auto pArea = mw->getLocalArea(this, currently_dragged_point->getBoundsInParent());
	auto tl = pArea.getCentre();
	tl.addXY(-100, -100);

	touchOverlay->setTopLeftPosition(tl);
#endif
}

//==============================================================================
TableEditor::DragPoint::DragPoint(TableEditor& parent, bool isStart_, bool isEnd_):
	normalizedGraphPoint(-1, -1, 0.5),
	isStart(isStart_),
	isEnd(isEnd_),
	over(false),
	dragPlotSize({}),
	constantValue(-1.0f)
{
	const int size = isStartOrEnd() ? parent.dragProperties.endPointSize : parent.dragProperties.dragPointSize;
	setSize(size, size);
}

TableEditor::DragPoint::~DragPoint()
{
	masterReference.clear();
}

void TableEditor::DragPoint::paint (Graphics& g)
{
	auto te = findParentComponentOfClass<TableEditor>();

	if (te == nullptr)
		return;
	
	if(auto l = te->getTableLookAndFeel())
		l->drawTablePoint(g, *te, getLocalBounds().toFloat(), isStartOrEnd(), over, false);
}

void TableEditor::DragPoint::resized()
{
    // This method is where you should set the bounds of any child
    // components that your component contains..

}

TableEditor::TouchOverlay::TouchOverlay(DragPoint* point)
{
#if !HISE_NO_GUI_TOOLS
	table = point->findParentComponentOfClass<TableEditor>();

	addAndMakeVisible(curveSlider = new Slider());

	curveSlider->setSliderStyle(Slider::SliderStyle::LinearBarVertical);
	curveSlider->setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
	curveSlider->setColour(Slider::ColourIds::backgroundColourId, Colours::transparentBlack);
	curveSlider->setColour(Slider::ColourIds::thumbColourId, Colours::white.withAlpha(0.1f));
	curveSlider->setColour(Slider::ColourIds::trackColourId, Colours::white.withAlpha(0.3f));
	curveSlider->setRange(0.0, 1.0, 0.01);
	curveSlider->setValue(point->getCurve(), dontSendNotification);

	addAndMakeVisible(deletePointButton = new ShapeButton("Delete", Colours::white.withAlpha(0.4f), Colours::white.withAlpha(0.8f), Colours::white));

	curveSlider->addListener(this);
	deletePointButton->addListener(this);

	Path p;
	p.loadPathFromData(HiBinaryData::ProcessorEditorHeaderIcons::closeIcon, sizeof(HiBinaryData::ProcessorEditorHeaderIcons::closeIcon));

	setInterceptsMouseClicks(false, true);

	deletePointButton->setShape(p, false, true, true);

	setSize(200, 200);
#endif
}


void TableEditor::TouchOverlay::resized()
{
	if (auto te = table.getComponent())
	{
		if (auto dp = te->currently_dragged_point)
		{
			deletePointButton->setVisible(!dp->isStartOrEnd());
		}
	}

	auto area = getLocalBounds();
	curveSlider->setBounds(area.removeFromLeft(40));
	deletePointButton->setBounds(area.removeFromRight(50).removeFromTop(50));
}



void TableEditor::TouchOverlay::buttonClicked(Button* /*b*/)
{
	if (auto te = table.getComponent())
	{
		if(auto dp = te->currently_dragged_point)
		{
			te->removeDragPoint(dp);
			te->closeTouchOverlay();
		}
	}
}


void TableEditor::TouchOverlay::sliderValueChanged(Slider* slider)
{
	if (auto te = table.getComponent())
	{
		if (auto dp = te->currently_dragged_point)
		{
			dp->setCurve((float)slider->getValue());
			te->updateTable(true);
			te->refreshGraph();
		}
	}
}


bool TableEditor::TableAction::perform()
{
	if (table.getComponent() == nullptr)
		return false;

	bool refresh = false;

	switch (what)
	{
	case hise::TableEditor::TableAction::Add:
		table->addDragPoint((int)x, (int)y, curve, false, false, false);
		refresh = true;
		break;
	case hise::TableEditor::TableAction::Delete:
	{
		auto dp = table->getPointUnder((int)oldX, (int)oldY);
		
		if (dp != nullptr)
			table->removeDragPoint(dp, false);

		refresh = true;
		break;
	}
		
	case hise::TableEditor::TableAction::Drag:
		table->changePointPosition(index, x, y, false);
		break;
	case hise::TableEditor::TableAction::Curve:
		table->updateCurve((int)x, (int)y, curve, false);
		break;
	case hise::TableEditor::TableAction::numActions:
		break;
	default:
		break;
	}

	if (refresh)
	{
		table->updateTable(false);
		table->refreshGraph();

		table->needsRepaint = true;
		table->repaint();
	}

	return true;
}


bool TableEditor::TableAction::undo()
{
	if (table.getComponent() == nullptr)
		return false;

	bool refresh = false;

	switch (what)
	{
	case hise::TableEditor::TableAction::Add:
	{
		auto dp = table->getPointUnder((int)x, (int)y);

		refresh = true;

		if (dp != nullptr)
			table->removeDragPoint(dp, false);

		break;
	}
	case hise::TableEditor::TableAction::Delete:
		table->addDragPoint((int)oldX, (int)oldY, oldCurve, false, false, false);
		refresh = true;
		break;
	case hise::TableEditor::TableAction::Drag:
		table->changePointPosition(index, oldX, oldY, false);
		break;
	case hise::TableEditor::TableAction::Curve:
		table->updateCurve((int)x, (int)y, oldCurve, false);
		break;
	case hise::TableEditor::TableAction::numActions:
		break;
	default:
		break;
	}

	if (refresh)
	{
		table->updateTable(false);
		table->refreshGraph();

		table->needsRepaint = true;
		table->repaint();
	}

	return true;
}

TableEditor::LookAndFeelMethods::~LookAndFeelMethods()
{}

bool TableEditor::LookAndFeelMethods::shouldClosePath() const
{ return true; }

TableEditor::HiseTableLookAndFeel::~HiseTableLookAndFeel()
{}

TableEditor::EditListener::~EditListener()
{}

void TableEditor::EditListener::pointDragStarted(Point<int> position, float index, float value)
{
	ignoreUnused(position, index, value);
}

void TableEditor::EditListener::pointDragEnded()
{
}

void TableEditor::EditListener::pointDragged(Point<int> position, float index, float value)
{
	ignoreUnused(position, index, value);
}

void TableEditor::EditListener::curveChanged(Point<int> position, float curveValue)
{
	ignoreUnused(position, curveValue);
}

void TableEditor::setComplexDataUIBase(ComplexDataUIBase* newData)
{
	if (auto t = dynamic_cast<Table*>(newData))
		setEditedTable(t);
}

void TableEditor::changeListenerCallback(SafeChangeBroadcaster* b)
{
	if (dynamic_cast<Table*>(b) != nullptr)
	{
		createDragPoints();

		refreshGraph();

		return;
	}
}

Table* TableEditor::getEditedTable() const
{
	return editedTable.get();
}

void TableEditor::setEditedTable(Table* newTable)
{
	if (editedTable != nullptr)
		editedTable->removeRulerListener(this);

	editedTable = newTable;

	if (editedTable != nullptr)
	{
		editedTable->addRulerListener(this);
		createDragPoints();
		refreshGraph();
		setDisplayedIndex(editedTable->getUpdater().getLastDisplayValue());
	}
}

void TableEditor::indexChanged(float newIndex)
{
	setDisplayedIndex(newIndex);
}

String TableEditor::getObjectTypeName()
{ 
	return "Table Data"; 
}

void TableEditor::setMouseDragProperties(const var& obj)
{
	dragProperties.fromVar(obj);

	setMargin(dragProperties.margin);

	if(dragProperties.numSteps != -1)
	{
		snapValues.clear();

		if(dragProperties.numSteps > 0)
		{
			for(int i = 0; i < dragProperties.numSteps; i++)
			{
				auto v = (float)i / (float)dragProperties.numSteps;
				snapValues.add(v);
			}

			snapValues.add(1.0f);
		}
	}

	auto bothDefined = dragProperties.fixLeftEdge > -0.5f &&
					   dragProperties.fixRightEdge > -0.5f;

	if (dragProperties.fixLeftEdge > -0.5f)
	{
		if(editedTable != nullptr)
			editedTable->setTablePoint(0, 0.0, dragProperties.fixLeftEdge, 0.5);
	}
		

	if (dragProperties.fixRightEdge > -0.5f)
	{
		if (editedTable != nullptr)
		{
			auto numPoints = editedTable->getNumGraphPoints();
			auto lastCurve = editedTable->getGraphPoint(numPoints-1).curve;
			editedTable->setTablePoint(numPoints-1, 1.0, dragProperties.fixRightEdge, lastCurve);
		}
	}
	
	if(bothDefined && editedTable != nullptr)
		editedTable->setStartAndEndY(dragProperties.fixLeftEdge, dragProperties.fixRightEdge);
}

void TableEditor::setDrawTableValueLabel(bool shouldBeDisplayed)
{
	displayPopup = shouldBeDisplayed;
}

bool TableEditor::shouldDrawTableValueLabel() const
{ return displayPopup; }

void TableEditor::copyAction()
{ SystemClipboard::copyTextToClipboard(getEditedTable()->exportData()); }

void TableEditor::pasteAction()
{
	const String data = SystemClipboard::getTextFromClipboard();
	getEditedTable()->restoreData(data);

	createDragPoints();
	refreshGraph();
}

void TableEditor::setMargin(float newMargin)
{
	dragProperties.margin = newMargin;
}

Rectangle<float> TableEditor::getTableArea() const
{
	return getLocalBounds().toFloat().reduced(dragProperties.margin);
}

void TableEditor::setUseFlatDesign(bool shouldUseFlatDesign)
{
	useFlatDesign = shouldUseFlatDesign;
	invalidatePaintCache();
	repaint();
}

void TableEditor::setLineThickness(float newLineThickness)
{
	lineThickness = newLineThickness;
	invalidatePaintCache();
	repaint();
}

void TableEditor::setFont(Font newFont)
{
	fontToUse = newFont;
}

UndoManager* TableEditor::getUndoManager(bool useUndoManager)
{
	if (!useUndoManager)
		return nullptr;

	if (auto d = getEditedTable())
		return getEditedTable()->getUndoManager();

	return nullptr;
}

void TableEditor::changePointPosition(int index, float x, float y, bool useUndoManager)
{
	if (index == -1 || index >= drag_points.size())
		return;

	if (auto um = getUndoManager(useUndoManager))
	{
		auto oldPos = drag_points[index]->getPos();

		um->perform(new TableAction(this, TableAction::Drag, index, x, y, 0.0f, oldPos.getX(), oldPos.getY(), 0.0f));
	}
	else
	{
		drag_points[index]->changePos(Point<float>(x, y));

		updateTouchOverlayPosition();

		updateTable(false); // Only update small tables while dragging
		refreshGraph();

		needsRepaint = true;
		repaint();
	}
}

void TableEditor::setScrollModifiers(ModifierKeys newScrollModifiers)
{
	scrollModifiers = newScrollModifiers;
}

void TableEditor::setScrollWheelEnabled(bool shouldBeEnabled)
{
	ModifierKeys mod;

	if (!shouldBeEnabled)
		mod.withFlags(ModifierKeys::commandModifier);

	setScrollModifiers(mod);
}

void TableEditor::renderStaticContent(Graphics& g)
{
	if (auto l = getTableLookAndFeel())
	{
		l->drawTableBackground(g, *this, getTableArea(), this->ruler->getValue());
		l->drawTablePath(g, *this, dragPath, getTableArea(), lineThickness);

		for(const auto& mp: mid_points)
		{
			auto dragged = mp.first == draggedMidPointIndex;
			auto hovered = mp.first == hoveredMidPointIndex;

			l->drawTableMidPoint(g, *this, mp.second, hovered, dragged);
		}
	}
}

void TableEditor::invalidatePaintCache()
{
	paintCacheValid = false;
}

void TableEditor::colourChanged()
{
	invalidatePaintCache();
	repaint();
}

void TableEditor::lookAndFeelChanged()
{
	invalidatePaintCache();
	repaint();
}

void TableEditor::paintOverChildren(Graphics& g)
{
	
}

void TableEditor::addEditListener(EditListener* l)
{
	editListeners.addIfNotAlreadyThere(l);
}

void TableEditor::removeEditListener(EditListener* l)
{
	editListeners.removeAllInstancesOf(l);
}

float TableEditor::getLastIndex() const
{ return jlimit(0.0f, 1.0f, lastIndex); }

TableEditor::LookAndFeelMethods* TableEditor::getTableLookAndFeel()
{
	if (auto lm = getSpecialLookAndFeel<LookAndFeelMethods>(this))
		return lm;

	return &defaultLaf;
}

TableEditor::Ruler::Ruler():
	value(0.0f)
{
	setInterceptsMouseClicks(false, false);
}

double TableEditor::Ruler::getValue()
{return value;}

void TableEditor::Ruler::requestFrames(bool shouldRun)
{
	if (wantsFrames == shouldRun)
		return;

	wantsFrames = shouldRun;
	DisplayLinkClock::getInstance()->setNeedsFrames(this, shouldRun);
}

void TableEditor::Ruler::setIndex(float newIndex)
{
	// This arrives on the pooled UI timer for as long as the LFO runs. In playhead mode it
	// is only used as the wake-up: when notes are being held, ask the display link for
	// frames; onDisplayFrame() releases them again once the playhead hides.
	if (auto te = findParentComponentOfClass<TableEditor>())
	{
		if (auto table = te->getEditedTable())
		{
			const auto ps = table->getPlayheadState();

			if (ps.active)
			{
				setPlayheadActive(te, true);
				value = newIndex;

				if (ps.playing)
					requestFrames(true);

				return;
			}
		}
	}

	if (playheadActive)
	{
		value = newIndex;
		return;
	}

	if (newIndex != value)
	{
		value = newIndex;

		SafeAsyncCall::repaint(this);
	}
}

TableEditor::Ruler::~Ruler()
{
	if (auto c = DisplayLinkClock::getInstanceWithoutCreating())
		c->removeListener(this);
}

Rectangle<int> TableEditor::Ruler::getPlayheadStrip(TableEditor* te, double position) const
{
	const auto area = te->getTableArea();
	const int x = roundToInt(area.getX() + position * area.getWidth());
	const int pad = roundToInt(te->lineThickness) + 6;

	return Rectangle<int>(x - pad, 0, 2 * pad, getHeight());
}

void TableEditor::Ruler::setPlayheadActive(TableEditor* te, bool shouldBeActive)
{
	if (playheadActive == shouldBeActive)
		return;

	playheadActive = shouldBeActive;
	hasPlayheadPos = false;

	te->invalidatePaintCache();
	te->repaint();
}

void TableEditor::Ruler::onDisplayFrame(double frameTimeMs, double deltaMs)
{
	auto te = findParentComponentOfClass<TableEditor>();

	if (te == nullptr)
		return;

	auto table = te->getEditedTable();

	if (table == nullptr)
	{
		setPlayheadActive(te, false);
		return;
	}

	const auto ps = table->getPlayheadState();

	if (!ps.active)
	{
		setPlayheadActive(te, false);
		requestFrames(false);
		return;
	}

	if (!isShowing())
	{
		requestFrames(false);   // setIndex() wakes us again when we are back on screen
		return;
	}

	setPlayheadActive(te, true);

	auto wrap = [](double v) { return v - std::floor(v); };
	auto constrain = [&](double v) { return ps.loops ? wrap(v) : jlimit(0.0, 1.0, v); };

	// Where the audio thread says the playhead is *now*: the last reported position plus
	// the time elapsed since it was reported. Held if the audio thread has gone quiet.
	const double age = frameTimeMs - ps.timestampMs;
	const bool stale = age < 0.0 || age > 500.0;
	const double target = constrain(stale ? ps.position : ps.position + ps.cyclesPerSecond * age * 0.001);

	const bool resetHappened = ps.generation != playheadGeneration;
	playheadGeneration = ps.generation;

	double next;

	if (!hasPlayheadPos || stale || resetHappened)
	{
		next = target;
	}
	else
	{
		// Advance on the display clock only. Hosts render audio in bursts, so the reported
		// position leads or lags real time by a block or two; jumping to it shows as judder.
		// Instead the error just trims the speed (+-15 %) until the gap is closed. Real
		// discontinuities (retrigger, phase knob) arrive through the generation counter.
		double err = target - playheadPos;

		if (ps.loops)
		{
			if (err > 0.5)       err -= 1.0;
			else if (err < -0.5) err += 1.0;
		}

		const double trim = jlimit(-0.15, 0.15, err * 1.5);

		next = constrain(playheadPos + ps.cyclesPerSecond * (1.0 + trim) * deltaMs * 0.001);
	}

	// hidden while no notes are held, and once a one-shot LFO has finished its cycle
	const bool nowVisible = ps.playing && (ps.loops || ps.position < 1.0);

	const bool visibilityChanged = nowVisible != playheadVisible;

	if (visibilityChanged && nowVisible)
		next = target;   // reappearing: nobody sees this jump, so start exactly on the audio position

	// frames are only needed while the playhead is on screen; setIndex() re-requests them
	requestFrames(nowVisible);

	if (hasPlayheadPos && next == playheadPos && !visibilityChanged)
		return;

	const auto oldStrip = getPlayheadStrip(te, hasPlayheadPos ? playheadPos : next);
	const auto newStrip = getPlayheadStrip(te, next);

	playheadPos = next;
	value = (float)next;
	playheadVisible = nowVisible;
	hasPlayheadPos = true;

	if (!nowVisible && !visibilityChanged)
		return;   // keep tracking silently, nothing to draw

	te->playheadClip = oldStrip.getUnion(newStrip);

	repaint(oldStrip);
	repaint(newStrip);
}

Point<float> TableEditor::DragPoint::getPos() const
{
	//jassert( !dragPlotSize.isEmpty() );

	const float x_pos = normalizedGraphPoint.x * dragPlotSize.getWidth() + dragPlotSize.getX();
	const float y_pos = (1.0f - normalizedGraphPoint.y) * dragPlotSize.getHeight() + dragPlotSize.getY();

	return Point<float>(x_pos, y_pos);
}

void TableEditor::DragPoint::changePos(Point<float> newPosition)
{
	jassert( !dragPlotSize.isEmpty() );

	// Check if the x value can be changed
	if(! isStartOrEnd())
		normalizedGraphPoint.x = (newPosition.getX() - dragPlotSize.getX()) / dragPlotSize.getWidth();

	normalizedGraphPoint.y = 1.0f - ((newPosition.getY() - dragPlotSize.getY()) / dragPlotSize.getHeight());

	this->setCentrePosition((int)getPos().getX(), (int)getPos().getY());
}

void TableEditor::DragPoint::setPosPixel(Point<float> newPosition)
{
	//jassert( !dragPlotSize.isEmpty() );

	normalizedGraphPoint.x = (newPosition.getX() - dragPlotSize.getX()) / dragPlotSize.getWidth();
	normalizedGraphPoint.y = 1.0f - (newPosition.getY() - dragPlotSize.getY()) / dragPlotSize.getHeight();

	this->setCentrePosition((int)getPos().getX(), (int)getPos().getY());
}

void TableEditor::DragPoint::setPosNormalized(Point<float> normalizedPoint)
{
	jassert(!dragPlotSize.isEmpty());

	normalizedGraphPoint.x = normalizedPoint.x;
	normalizedGraphPoint.y = normalizedPoint.y;

	this->setCentrePosition((int)getPos().getX(), (int)getPos().getY());
}

void TableEditor::DragPoint::updateCurve(float newCurveValue)
{
	normalizedGraphPoint.curve += (newCurveValue * 0.1f);
	normalizedGraphPoint.curve = jlimit<float>(0.0f, 1.0f, normalizedGraphPoint.curve);
}

void TableEditor::DragPoint::setCurve(float newValue)
{
	normalizedGraphPoint.curve = jlimit<float>(0.0f, 1.0f, newValue);
}

float TableEditor::DragPoint::getCurve() const
{
	return normalizedGraphPoint.curve;
}

void TableEditor::DragPoint::setTableEditorSize(Rectangle<float> tableEditorBounds)
{
	dragPlotSize = tableEditorBounds;
}

void TableEditor::DragPoint::mouseEnter(const MouseEvent& mouseEvent)
{
	if (auto te = findParentComponentOfClass<TableEditor>())
	{
		te->pointAreaBetweenMouse = {};
		setMouseCursor(MouseCursor(te->dragProperties.dragPointCursor));
	}

	over = true;
	repaint();
}

void TableEditor::DragPoint::mouseExit(const MouseEvent& mouseEvent)
{
	setMouseCursor(MouseCursor::NormalCursor);
	over = false;
	repaint();
}

bool TableEditor::DragPoint::isStartOrEnd() const
{ return ( isStart || isEnd ); }

const Table::GraphPoint& TableEditor::DragPoint::getGraphPoint() const
{ return normalizedGraphPoint; }

bool TableEditor::DragPoint::canBeModified(const MouseDragProperties& properties) const
{
	if (isStart)
		return properties.fixLeftEdge < -0.5f;
	if (isEnd)
		return properties.fixRightEdge < -0.5f;

	return true;
}

TableEditor::DragPointComparator::DragPointComparator()
{}

int TableEditor::DragPointComparator::compareElements(DragPoint* dp1, DragPoint* dp2)
{
	if(dp1->getGraphPoint().x < dp2->getGraphPoint().x) return -1;
	else if(dp1->getGraphPoint().x > dp2->getGraphPoint().x) return 1;
	else return 0;
}

int TableEditor::getDraggedMidPointIndex(const MouseEvent& e) const
{
	for (const auto& mp: mid_points)
	{
		if (mp.second.contains(e.getPosition().toFloat()))
			return mp.first;
	}

	return -1;
}

TableEditor::DragPoint* TableEditor::getPointUnder(int x, int y)
{
	DragPoint *dp = static_cast<DragPoint*>(this->getComponentAt(x, y));

	for (int i = 0; i < drag_points.size(); i++)
	{
		if (dp == drag_points[i]) 
			return dp;
	}

	return nullptr;
}

TableEditor::DragPoint* TableEditor::getNextPointFor(int x) const
{
	for (int i = 0; i < drag_points.size() - 1; i++)
	{
		auto dp = drag_points[i];
		auto next = drag_points[i + 1];

		if (x >= dp->getX() && x <= next->getX())
			return next;
	}

	return nullptr;
}

TableEditor::DragPoint* TableEditor::getPrevPointFor(int x) const
{
	for (int i = 0; i < drag_points.size() - 1; i++)
	{
		auto dp = drag_points[i];

		auto next = drag_points[i + 1];

		if (x >= dp->getX() && x <= next->getX())
			return dp;
	}

	return nullptr;
}

void TableEditor::addNormalizedDragPoint(Table::GraphPoint normalizedPoint, bool isStart, bool isEnd)
{
	auto a = getTableArea();
	addDragPoint((int)(a.getX() + normalizedPoint.x * a.getWidth()), (int)(a.getY() + (1.0f - normalizedPoint.y) * a.getHeight()), normalizedPoint.curve, isStart, isEnd);
}

Array<Table::GraphPoint> TableEditor::getDrawShapePoints(int shapeId, float left, float right, float amplitude) const
{
	Array<Table::GraphPoint> p;

	const float w = right - left;
	const float mid = left + w * 0.5f;
	const float tiny = jmax(1.0e-4f, w * 0.001f);

	// y: 1 = top, 0 = bottom. curve 0.5 = linear (factor applies to the segment to
	// the LEFT of the point), so the left edge of every shape stays 0.5 to keep the
	// join with the neighbouring cell linear.
	switch (shapeId)
	{
	case 1: // ramp up
		p.add(Table::GraphPoint(left, 0.0f, 0.5f));
		p.add(Table::GraphPoint(right, 1.0f, 0.5f));
		break;
	case 2: // ramp down
		p.add(Table::GraphPoint(left, 1.0f, 0.5f));
		p.add(Table::GraphPoint(right, 0.0f, 0.5f));
		break;
	case 3: // triangle
		p.add(Table::GraphPoint(left, 0.0f, 0.5f));
		p.add(Table::GraphPoint(mid, 1.0f, 0.5f));
		p.add(Table::GraphPoint(right, 0.0f, 0.5f));
		break;
	case 4: // square
		p.add(Table::GraphPoint(left, 1.0f, 0.5f));
		p.add(Table::GraphPoint(mid, 1.0f, 0.5f));
		p.add(Table::GraphPoint(jmin(mid + tiny, right), 0.0f, 0.5f));
		p.add(Table::GraphPoint(right, 0.0f, 0.5f));
		break;
	case 5: // sine (one cycle, starts at centre rising)
	{
		const int n = 16;
		for (int i = 0; i <= n; i++)
		{
			const float t = (float)i / (float)n;
			const float x = left + t * w;
			const float y = 0.5f + 0.5f * std::sin(t * MathConstants<float>::twoPi);
			p.add(Table::GraphPoint(x, y, 0.5f));
		}
		break;
	}
	case 6: // exp up
		p.add(Table::GraphPoint(left, 0.0f, 0.5f));
		p.add(Table::GraphPoint(right, 1.0f, 0.75f));
		break;
	case 7: // exp down
		p.add(Table::GraphPoint(left, 1.0f, 0.5f));
		p.add(Table::GraphPoint(right, 0.0f, 0.75f));
		break;
	case 8: // column (flat plateau across the whole cell; the vertical sides are formed
	        // by the cell-boundary steps to the neighbouring cells)
		p.add(Table::GraphPoint(left, 1.0f, 0.5f));
		p.add(Table::GraphPoint(right, 1.0f, 0.5f));
		break;
	case 9: // exp up mirrored (curve 0.25 = concave: slow start, steep finish)
		p.add(Table::GraphPoint(left, 0.0f, 0.5f));
		p.add(Table::GraphPoint(right, 1.0f, 0.25f));
		break;
	case 10: // exp down mirrored (curve 0.25 = stays high, steep finish)
		p.add(Table::GraphPoint(left, 1.0f, 0.5f));
		p.add(Table::GraphPoint(right, 0.0f, 0.25f));
		break;
	case 11: // valley (inverted triangle)
		p.add(Table::GraphPoint(left, 1.0f, 0.5f));
		p.add(Table::GraphPoint(mid, 0.0f, 0.5f));
		p.add(Table::GraphPoint(right, 1.0f, 0.5f));
		break;
	default:
		break;
	}

	// Scale the height by amplitude. Most shapes sit on the y = 0 baseline, so this is
	// a uniform multiply and amplitude is the height of the peak. Sine is anchored at
	// its centre (0.5) instead, so its edges stay at 0.5 at any amplitude - that keeps
	// a row of sines joining cleanly even when neighbouring cells differ in height.
	amplitude = jlimit(0.0f, 1.0f, amplitude);

	if (amplitude != 1.0f)
	{
		const float anchor = (shapeId == 5) ? 0.5f : 0.0f;

		for (auto& gp : p)
			gp.y = anchor + (gp.y - anchor) * amplitude;
	}

	return p;
}

void TableEditor::stampDrawShapeAt(float normalizedX, float amplitude)
{
	const int shapeId = dragProperties.drawShape;

	if (shapeId == 0 || snapValues.size() < 2 || editedTable.get() == nullptr)
		return;

	normalizedX = jlimit(0.0f, 1.0f, normalizedX);

	// Find the grid cell [left, right] containing the click (snapValues are sorted
	// ascending and are expected to span 0..1, e.g. eighths = nine values).
	float left = 0.0f, right = 1.0f;
	int cellIndex = -1;

	for (int i = 0; i < snapValues.size() - 1; i++)
	{
		const float a = (float)snapValues[i];
		const float b = (float)snapValues[i + 1];

		if (normalizedX >= a && normalizedX <= b)
		{
			left = a;
			right = b;
			cellIndex = i;
			break;
		}
	}

	if (cellIndex == -1 || right <= left)
		return; // outside the gridded range

	// Quantise the height so micro-jitter during a drag does not re-stamp, but a real
	// vertical move (changing the cursor height within a cell) still updates it live.
	const float ampQuant = floorf(jlimit(0.0f, 1.0f, amplitude) * 256.0f) / 256.0f;

	if (cellIndex == lastStampedCell && fabsf(ampQuant - lastStampedAmp) < 1.0e-4f)
		return; // same cell and same height already stamped during the current gesture

	lastStampedCell = cellIndex;
	lastStampedAmp = ampQuant;

	const float eps = jmax(1.0e-5f, (right - left) * 0.0001f);

	// Width of the discontinuity between two adjacent cells. Each interior grid line
	// B is shared by two cells, but a table holds one y per x, so a shape whose left
	// and right edges differ (ramp / square / exp) cannot simply own the point at B -
	// doing so overwrites the neighbour's value and flattens it. Convention: the cell
	// to the LEFT of B owns the point exactly at B; the cell to the RIGHT starts at
	// B + step. That keeps both cells intact and draws a near-vertical step between
	// them (so adjacent ramps make a sawtooth). Shapes with matching edges (triangle,
	// sine) are unaffected either way.
	const float step = jlimit(0.0025f, (right - left) * 0.4f, (right - left) * 0.05f);

	const bool leftIsInterior = left > eps;             // not the table start (x = 0)
	const bool rightIsInterior = right < 1.0f - eps;    // not the table end   (x = 1)

	// Keep the neighbours; drop the cell interior, the point exactly at `right` (this
	// cell's own old right edge), and the table start/end that the shape will replace.
	Array<Table::GraphPoint> keepLeft, keepRight;

	for (auto* dp : drag_points)
	{
		auto gp = dp->getGraphPoint();

		if (gp.x < left - eps)
			keepLeft.add(gp);                                       // entirely left
		else if (gp.x > right + eps)
			keepRight.add(gp);                                      // entirely right (incl. right neighbour's B + step start)
		else if (leftIsInterior && fabsf(gp.x - left) <= eps)
			keepLeft.add(gp);                                       // left neighbour's boundary point at `left`
		// else: drop
	}

	const bool leftOwned = !keepLeft.isEmpty() && fabsf(keepLeft.getLast().x - left) <= eps;

	// Decide how the shape joins the left neighbour. Peek the shape's left-edge value
	// (independent of the x range): if it matches the neighbour's boundary value, the
	// two cells share a SINGLE point (smooth join - e.g. a row of sines); otherwise
	// the shape is inset by `step` so they form a clean vertical step (e.g. ramps ->
	// sawtooth). This removes the redundant double point / flat spot where cells meet
	// at the same level.
	auto shapePts = getDrawShapePoints(shapeId, left, right, amplitude);
	const float shapeLeftVal = shapePts.getFirst().y;

	const float joinEps = 1.0e-3f;
	const bool leftJoin = leftIsInterior && leftOwned && fabsf(keepLeft.getLast().y - shapeLeftVal) <= joinEps;

	float innerLeft = left;
	if (leftIsInterior && leftOwned && !leftJoin)
		innerLeft = jmin(left + step, right);   // values differ -> vertical step
	else if (leftJoin)
		keepLeft.removeLast();                  // values match -> drop the duplicate, share one point

	if (innerLeft != left)
		shapePts = getDrawShapePoints(shapeId, innerLeft, right, amplitude);

	Array<Table::GraphPoint> merged;
	merged.addArray(keepLeft);
	merged.addArray(shapePts);
	merged.addArray(keepRight);

	struct GraphPointXComparator
	{
		static int compareElements(const Table::GraphPoint& a, const Table::GraphPoint& b)
		{
			if (a.x < b.x) return -1;
			if (a.x > b.x) return 1;
			return 0;
		}
	};

	GraphPointXComparator comparator;
	merged.sort(comparator);

	if (merged.size() < 2)
		return;

	// Keep the loop seamless without distorting the drawn shape. x = 0 and x = 1 are
	// the same phase point of a looping LFO, so their y must match or the wrap clicks.
	// Shapes whose boundary edge does not already sit on the y = 0 baseline get a
	// near-vertical grounding point in the first and/or last cell (per shape) so both
	// global endpoints land at 0 while the shape itself is preserved. Shapes whose
	// edges already match (triangle 0/0, sine 0.5/0.5, valley 1/1) are left untouched.
	if (dragProperties.syncStartEnd)
	{
		// front = add (0,0) at the start of the first cell; end = add (1,0) at the
		// end of the last cell. ramp/exp follow their direction; square + column are
		// self-contained bars (both).
		const bool groundsFront = (shapeId == 2 || shapeId == 4 || shapeId == 7 || shapeId == 8 || shapeId == 10);
		const bool groundsEnd   = (shapeId == 1 || shapeId == 4 || shapeId == 6 || shapeId == 8 || shapeId == 9);

		const float groundEps = 1.0e-4f;

		if (cellIndex == 0 && groundsFront && merged.getFirst().y > groundEps)
		{
			merged.getReference(0).x = jmin(step, merged[1].x);          // steep rise from 0
			merged.insert(0, Table::GraphPoint(0.0f, 0.0f, 0.5f));
		}

		if (cellIndex == snapValues.size() - 2 && groundsEnd && merged.getLast().y > groundEps)
		{
			const int li = merged.size() - 1;
			merged.getReference(li).x = jmax(1.0f - step, merged[li - 1].x); // steep fall to 0
			merged.add(Table::GraphPoint(1.0f, 0.0f, 0.5f));
		}
	}

	drag_points.clear();

	addNormalizedDragPoint(merged.getFirst(), true, false);

	for (int i = 1; i < merged.size() - 1; i++)
		addNormalizedDragPoint(merged[i], false, false);

	addNormalizedDragPoint(merged.getLast(), false, true);

	updateTable(true);
	refreshGraph();

	needsRepaint = true;
	repaint();

	editedTable->sendGraphUpdateMessage();
}

void TableEditor::updateTable(bool refreshLookUpTable)
{
	ScopedPointer<DragPointComparator> dpc = new DragPointComparator();

	drag_points.sort(*dpc);

	Array<Table::GraphPoint> newPoints;

	for(int i = 0; i < drag_points.size(); i++)	
		newPoints.add(drag_points[i]->getGraphPoint());

	if(editedTable.get() != nullptr) 
		editedTable->setGraphPoints(newPoints, drag_points.size(), refreshLookUpTable);
}

TableEditor::TableAction::TableAction(TableEditor* table_, Action what_, int index_, float x_, float y_, float curve_,
	float oldX_, float oldY_, float oldCurve_):
	table(table_),
	what(what_),
	index(index_),
	x(x_),
	y(y_),
	curve(curve_),
	oldX(oldX_),
	oldY(oldY_),
	oldCurve(oldCurve_)
{}

void TableEditor::LookAndFeelMethods::drawTableBackground(Graphics& g, TableEditor& te, Rectangle<float> area, double rulerPosition)
{
	if(te.useFlatDesign)
	{
		g.setColour(te.findColour(ColourIds::bgColour));
		g.fillAll();
	}
	else
	{
		g.setColour(Colours::lightgrey.withAlpha(0.1f));
		g.drawRect(area, 1);	
	}
}

void TableEditor::LookAndFeelMethods::drawTablePath(Graphics& g, TableEditor& te, Path& p, Rectangle<float> area, float )
{
    if(te.useFlatDesign)
    {
        g.setColour(te.findColour(ColourIds::fillColour));
        g.fillPath(p);
        g.setColour(te.findColour(ColourIds::lineColour));
        g.strokePath(p, PathStrokeType(te.lineThickness));
    }
    else
    {
        GlobalHiseLookAndFeel::fillPathHiStyle(g, p, area.getWidth(), area.getHeight());
    }
}

void TableEditor::LookAndFeelMethods::drawTablePoint(Graphics& g, TableEditor& te, Rectangle<float> tablePoint, bool isEdge, bool isHover, bool isDragged)
{
    const float width = (float)tablePoint.getWidth() - 6.0f;
    const float round = width * 0.2f;
    
    if(te.useFlatDesign)
    {
        g.setColour(te.findColour(TableEditor::ColourIds::lineColour));
        g.fillRoundedRectangle(tablePoint.reduced(3.0f), round);
    }
    else
    {
        if (isEdge)
        {
            g.setColour(Colours::white.withAlpha(0.3f));
            g.drawRoundedRectangle(3.0f, 3.0f, width, width, round, isHover ? 2.0f : 1.0f);
            g.setColour(Colours::white.withAlpha(0.2f));
            g.fillRoundedRectangle(3.0f, 3.0f, width, width, round);
        }
        else
        {
            g.setColour(Colours::white.withAlpha(0.3f));
            g.drawRoundedRectangle(3.0f, 3.0f, width, width, round, isHover ? 2.0f : 1.0f);
            g.setColour(Colours::white.withAlpha(0.2f));
            g.fillRoundedRectangle(3.0f, 3.0f, width, width, round);
        }
    }
	
}

void TableEditor::LookAndFeelMethods::drawTableMidPoint(Graphics& g, TableEditor& te, Rectangle<float> midPoint, bool isHover, bool isDragged)
{

	if (te.useFlatDesign)
	{
		g.setColour(te.findColour(TableEditor::ColourIds::lineColour));
		g.fillEllipse(midPoint.reduced(3.0f));
	}
	else
	{
		g.setColour(Colours::white.withAlpha(0.3f));
		g.drawEllipse(midPoint.reduced(3.0f), isHover ? 2.0f : 1.0f);
		g.setColour(Colours::white.withAlpha(0.2f));
		g.fillEllipse(midPoint.reduced(3.0f));
	}
}

void TableEditor::LookAndFeelMethods::drawTableRuler(Graphics& g, TableEditor& te, Rectangle<float> area, float lineThickness, double rulerPosition)
{
	float x = (float)rulerPosition * area.getWidth() + area.getX();
	float y = area.getY();
	float bottom = area.getBottom();

    if(te.useFlatDesign)
    {
        auto c = te.findColour(TableEditor::ColourIds::rulerColour);

        g.setColour(c);
        g.drawLine(Line<float>(x, y, x, bottom), lineThickness);
    }
    else
    {
        g.setColour(Colours::lightgrey.withAlpha(0.05f));
        g.fillRect(jmax(0.0f, x - 5.0f), y, 10.0f, area.getHeight());
        g.setColour(Colours::white.withAlpha(0.6f));

		g.drawLine(Line<float>(x, y, x, bottom), 0.5f);
    }
}

FileNameValuePropertyComponent::MyFunkyFilenameComponent::MyFunkyFilenameComponent(FileNameValuePropertyComponent& p,
	File::TypesOfFileToFind fileType_):
	parent(p),
	browseButton("Browse"),
	fileType(fileType_)
{
	addAndMakeVisible(&editor);
	editor.addListener(this);
	editor.setFont(GLOBAL_BOLD_FONT());
	editor.setSelectAllWhenFocused(true);

	editor.setTextToShowWhenEmpty("No folder selected", Colours::grey);

	addAndMakeVisible(&browseButton);
	browseButton.addListener(this);
	browseButton.setLookAndFeel(&alaf);
}

void FileNameValuePropertyComponent::MyFunkyFilenameComponent::buttonClicked(Button*)
{
	if (fileType == File::findDirectories)
	{
		FileChooser fileChooser("Select Folder");

		if (fileChooser.browseForDirectory())
		{
			parent.v = fileChooser.getResult().getFullPathName();
			parent.refresh();
		}
	}
	else
	{
		FileChooser fileChooser("Select Folder");

		if (fileChooser.browseForFileToOpen())
		{
			parent.v = fileChooser.getResult().getFullPathName();
			parent.refresh();
		}
	}
}

void FileNameValuePropertyComponent::MyFunkyFilenameComponent::textEditorReturnKeyPressed(TextEditor& textEditor)
{
	updateFromTextEditor();
}

void FileNameValuePropertyComponent::MyFunkyFilenameComponent::textEditorFocusLost(TextEditor& textEditor)
{
	updateFromTextEditor();
}

void FileNameValuePropertyComponent::MyFunkyFilenameComponent::textEditorTextChanged(TextEditor& textEditor)
{

}

void FileNameValuePropertyComponent::MyFunkyFilenameComponent::resized()
{
	auto area = getLocalBounds();

	browseButton.setBounds(area.removeFromRight(60));
	area.removeFromRight(5);
	editor.setBounds(area);
}

FileNameValuePropertyComponent::FileNameValuePropertyComponent(const String& name, File initialFile,
	File::TypesOfFileToFind ft, Value v_):
	PropertyComponent(name),
	fc(*this, ft),
	v(v_)
{
	addAndMakeVisible(fc);
}

void FileNameValuePropertyComponent::refresh()
{
	fc.editor.setText(v.getValue().toString(), dontSendNotification);
}

void FileNameValuePropertyComponent::MyFunkyFilenameComponent::updateFromTextEditor()
{
	auto t = editor.getText();

	if (t.isEmpty() || (File::isAbsolutePath(t) && File(t).isDirectory()))
	{
		parent.v = editor.getText();
	}
}

void TableEditor::LookAndFeelMethods::drawTableValueLabel(Graphics& g, TableEditor& te, Font f, const String& text, Rectangle<int> textBox)
{
    if(!te.shouldDrawTableValueLabel())
        return;
    
	g.setFont(f);
	g.setColour(te.findColour(TableEditor::ColourIds::overlayBgColour));
	g.fillRect(textBox);
	g.setColour(te.findColour(TableEditor::ColourIds::overlayTextId));
	g.drawRect(textBox, 1);
	g.drawText(text, textBox, Justification::centred, false);
}

} // namespace hise
