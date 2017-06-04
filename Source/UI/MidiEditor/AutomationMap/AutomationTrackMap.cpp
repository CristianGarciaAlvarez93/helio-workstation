/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "AutomationTrackMap.h"
#include "AutomationEventComponent.h"
#include "AutomationCurveHelper.h"
#include "ProjectTreeItem.h"
#include "MidiLayer.h"
#include "AutomationLayer.h"
#include "PlayerThread.h"
#include "MidiRoll.h"
#include "ComponentConnectorCurve.h"

#if HELIO_DESKTOP
#   define TRACKMAP_NOTE_COMPONENT_HEIGHT (1)
#   define TRACKMAP_TEMPO_EVENT_DIAMETER (25.f)
#   define TRACKMAP_TEMPO_HELPER_DIAMETER (15.f)
#elif HELIO_MOBILE
#   define TRACKMAP_NOTE_COMPONENT_HEIGHT (1)
#   define TRACKMAP_TEMPO_EVENT_DIAMETER (40.f)
#   define TRACKMAP_TEMPO_HELPER_DIAMETER (28.f)
#endif

#define DEFAULT_TRACKMAP_HEIGHT 128

AutomationTrackMap::AutomationTrackMap(ProjectTreeItem &parentProject, MidiRoll &parentRoll, WeakReference<MidiLayer> targetLayer) :
    project(parentProject),
    roll(parentRoll),
    layer(std::move(targetLayer)),
    projectFirstBeat(0.f),
    projectLastBeat(16.f),
    rollFirstBeat(0.f),
    rollLastBeat(16.f),
    draggingEvent(nullptr),
    addNewEventMode(false)
{
    this->setFocusContainer(false);
    this->setWantsKeyboardFocus(false);
    
    this->leadingConnector = new ComponentConnectorCurve(nullptr, nullptr);
    this->addAndMakeVisible(this->leadingConnector);
    
    this->setMouseCursor(MouseCursor::CopyingCursor);
    
    this->setOpaque(false);
    this->setInterceptsMouseClicks(true, true);
    
    this->reloadTrack();
    
    this->project.addListener(this);
    
    this->setSize(1, DEFAULT_TRACKMAP_HEIGHT);
}

AutomationTrackMap::~AutomationTrackMap()
{
    this->project.removeListener(this);
}


//===----------------------------------------------------------------------===//
// Component
//===----------------------------------------------------------------------===//

void AutomationTrackMap::mouseDown(const MouseEvent &e)
{
    if (e.mods.isLeftButtonDown())
    {
        this->insertNewEventAt(e);
    }
}

void AutomationTrackMap::mouseDrag(const MouseEvent &e)
{
    if (this->draggingEvent)
    {
        if (this->draggingEvent->isDragging())
        {
            this->draggingEvent->mouseDrag(e.getEventRelativeTo(this->draggingEvent));
        }
        else
        {
            this->draggingEvent->mouseDown(e.getEventRelativeTo(this->draggingEvent));
            this->setMouseCursor(MouseCursor(MouseCursor::DraggingHandCursor));
        }
    }
}

void AutomationTrackMap::mouseUp(const MouseEvent &e)
{
    if (this->draggingEvent != nullptr)
    {
        this->draggingEvent->mouseUp(e.getEventRelativeTo(this->draggingEvent));
        this->setMouseCursor(MouseCursor::CopyingCursor);
        this->draggingEvent = nullptr;
    }
}

void AutomationTrackMap::resized()
{
    this->rollFirstBeat = float(this->roll.getFirstBeat());
    this->rollLastBeat = float(this->roll.getLastBeat());
    
    this->setVisible(false);
    
    // во избежание глюков - сначала обновляем позиции
    for (int i = 0; i < this->eventComponents.size(); ++i)
    {
        AutomationEventComponent *const c = this->eventComponents.getUnchecked(i);
        c->setBounds(this->getEventBounds(c));
    }
    
    // затем - зависимые элементы
    for (int i = 0; i < this->eventComponents.size(); ++i)
    {
        AutomationEventComponent *const c = this->eventComponents.getUnchecked(i);
        c->updateConnector();
        c->updateHelper();
    }
    
    this->leadingConnector->resizeToFit();
    
    this->setVisible(true);
}

void AutomationTrackMap::mouseWheelMove(const MouseEvent &event, const MouseWheelDetails &wheel)
{
    this->roll.mouseWheelMove(event.getEventRelativeTo(&this->roll), wheel);
}


//===----------------------------------------------------------------------===//
// Event Helpers
//===----------------------------------------------------------------------===//

void AutomationTrackMap::insertNewEventAt(const MouseEvent &e)
{
    float draggingValue = 0;
    float draggingBeat = 0.f;
    this->getRowsColsByMousePosition(e.x - int(this->getEventDiameter() / 2),
                                     e.y - int(this->getEventDiameter() / 2),
                                     draggingValue, draggingBeat);
    
    AutomationLayer *activeAutoLayer = static_cast<AutomationLayer *>(this->layer.get());
    {
        this->addNewEventMode = true;
        activeAutoLayer->checkpoint();
        AutomationEvent event(activeAutoLayer, draggingBeat, draggingValue);
        activeAutoLayer->insert(event, true);
    }
}

void AutomationTrackMap::removeEventIfPossible(const AutomationEvent &e)
{
    AutomationLayer *autoLayer = static_cast<AutomationLayer *>(e.getLayer());
    
    if (autoLayer->size() > 1)
    {
        autoLayer->checkpoint();
        autoLayer->remove(e, true);
    }
}

float AutomationTrackMap::getEventDiameter() const
{
    return TRACKMAP_TEMPO_EVENT_DIAMETER;
}

float AutomationTrackMap::getHelperDiameter() const
{
    return TRACKMAP_TEMPO_HELPER_DIAMETER;
}

int AutomationTrackMap::getAvailableHeight() const
{
    return this->getHeight();
}

Rectangle<int> AutomationTrackMap::getEventBounds(AutomationEventComponent *event) const
{
    return this->getEventBounds(event->getBeat(), event->getControllerValue());
}

Rectangle<int> AutomationTrackMap::getEventBounds(float eventBeat, double controllerValue) const
{
    // hardcoded multiplier
    //const double multipliedCV = (controllerValue * 2.0) - 0.5;
    const double multipliedCV = controllerValue;
    
    const float diameter = this->getEventDiameter();
    const float rollLengthInBeats = (this->rollLastBeat - this->rollFirstBeat);
    const float projectLengthInBeats = (this->projectLastBeat - this->projectFirstBeat);
    
    const float beat = (eventBeat - this->rollFirstBeat);
    const float mapWidth = float(this->getWidth()) * (projectLengthInBeats / rollLengthInBeats);
    
    const int x = int((mapWidth * (beat / projectLengthInBeats)));
    const int fullh = this->getAvailableHeight();
    int y = int((1.0 - multipliedCV) * fullh); // upside down flip
    
    return Rectangle<int> (x - int(diameter / 2.f),
                           y - int(diameter / 2.f),
                           int(diameter),
                           int(diameter));
}

void AutomationTrackMap::getRowsColsByMousePosition(int x, int y, float &targetValue, float &targetBeat) const
{
    const float diameter = this->getEventDiameter();
    const float xRoll = float(x + (diameter / 2.f)) / float(this->getWidth()) * float(this->roll.getWidth());
	targetBeat = this->roll.getRoundBeatByXPosition(int(xRoll));
    
    // hardcoded multiplier
    //targetValue = float(y + (diameter / 2)) / float(this->getAvailableHeight());
    //targetValue = 0.75 - (targetValue / 2.0);
    //targetValue = jmin(jmax(targetValue, 0.25f), 0.75f);

    targetValue = float(this->getAvailableHeight() - y - (diameter / 2.f)) / float(this->getAvailableHeight()); // flip upside down
    targetValue = jmin(jmax(targetValue, 0.f), 1.f);
}


AutomationEventComponent *AutomationTrackMap::getPreviousEventComponent(int indexOfSorted) const
{
    const int indexOfPrevious = indexOfSorted - 1;
    
    return
        isPositiveAndBelow(indexOfPrevious, this->eventComponents.size()) ?
        this->eventComponents.getUnchecked(indexOfPrevious) :
        nullptr;
}

AutomationEventComponent *AutomationTrackMap::getNextEventComponent(int indexOfSorted) const
{
    const int indexOfNext = indexOfSorted + 1;
    
    return
        isPositiveAndBelow(indexOfNext, this->eventComponents.size()) ?
        this->eventComponents.getUnchecked(indexOfNext) :
        nullptr;
}


//===----------------------------------------------------------------------===//
// ProjectListener
//===----------------------------------------------------------------------===//

void AutomationTrackMap::onEventChanged(const MidiEvent &oldEvent, const MidiEvent &newEvent)
{
    if (newEvent.getLayer() == this->layer)
    {
        const AutomationEvent &autoEvent = static_cast<const AutomationEvent &>(oldEvent);
        const AutomationEvent &newAutoEvent = static_cast<const AutomationEvent &>(newEvent);
        
        if (AutomationEventComponent *component = this->eventsHash[autoEvent])
        {
            // update links and connectors
            this->eventComponents.sort(*component);
            const int indexOfSorted = this->eventComponents.indexOfSorted(*component, component);
            AutomationEventComponent *previousEventComponent(this->getPreviousEventComponent(indexOfSorted));
            AutomationEventComponent *nextEventComponent(this->getNextEventComponent(indexOfSorted));
            
            component->setNextNeighbour(nextEventComponent);
            //component->setVisible(false);
            this->updateTempoComponent(component);
            //component->setVisible(true);
            
            if (previousEventComponent)
            {
                previousEventComponent->setNextNeighbour(component);
                
                if (AutomationEventComponent *oneMorePrevious = this->getPreviousEventComponent(indexOfSorted - 1))
                {
                    oneMorePrevious->setNextNeighbour(previousEventComponent);
                }
            }
            
            if (nextEventComponent)
            {
                nextEventComponent->setNextNeighbour(this->getNextEventComponent(indexOfSorted + 1));
            }
            
            this->eventsHash.remove(autoEvent);
            this->eventsHash.set(newAutoEvent, component);
            
            if (indexOfSorted == 0 || indexOfSorted == 1)
            {
                this->leadingConnector->retargetAndUpdate(nullptr, this->eventComponents[0]);
            }
        }
    }
}

void AutomationTrackMap::onEventAdded(const MidiEvent &event)
{
    if (event.getLayer() == this->layer)
    {
        const AutomationEvent &autoEvent = static_cast<const AutomationEvent &>(event);
        
        auto component = new AutomationEventComponent(*this, autoEvent);
        this->addAndMakeVisible(component);

        // update links and connectors
        const int indexOfSorted = this->eventComponents.addSorted(*component, component);
        AutomationEventComponent *previousEventComponent(this->getPreviousEventComponent(indexOfSorted));
        AutomationEventComponent *nextEventComponent(this->getNextEventComponent(indexOfSorted));

        component->setNextNeighbour(nextEventComponent);
        this->updateTempoComponent(component);
        component->toFront(true);
        //this->eventAnimator.fadeIn(component, 150);
        
        if (previousEventComponent)
        {
            previousEventComponent->setNextNeighbour(component);
        }

        this->eventsHash.set(autoEvent, component);
        
        if (indexOfSorted == 0)
        {
            this->leadingConnector->retargetAndUpdate(nullptr, this->eventComponents[0]);
        }
        
        if (this->addNewEventMode)
        {
            this->draggingEvent = component;
            this->addNewEventMode = false;
        }
    }
}

void AutomationTrackMap::onEventRemoved(const MidiEvent &event)
{
    if (event.getLayer() == this->layer)
    {
        const AutomationEvent &autoEvent = static_cast<const AutomationEvent &>(event);
        
        if (AutomationEventComponent *component = this->eventsHash[autoEvent])
        {
            //this->eventAnimator.fadeOut(component, 150);
            this->removeChildComponent(component);
            this->eventsHash.remove(autoEvent);
            
            // update links and connectors for neighbours
            const int indexOfSorted = this->eventComponents.indexOfSorted(*component, component);
            AutomationEventComponent *previousEventComponent(this->getPreviousEventComponent(indexOfSorted));
            AutomationEventComponent *nextEventComponent(this->getNextEventComponent(indexOfSorted));
            
            if (previousEventComponent)
            {
                previousEventComponent->setNextNeighbour(nextEventComponent);
            }
            
            this->eventComponents.removeObject(component, true);
            
            if (this->eventComponents.size() > 0)
            {
                this->leadingConnector->retargetAndUpdate(nullptr, this->eventComponents[0]);
            }
        }
    }
}

void AutomationTrackMap::onLayerChanged(const MidiLayer *changedLayer)
{
    if (this->layer)
    {
        if (changedLayer == this->layer)
        {
            this->reloadTrack();
        }
    }
}

void AutomationTrackMap::onLayerAdded(const MidiLayer *addedLayer)
{
    if (this->layer)
    {
        if (addedLayer == this->layer)
        {
            if (addedLayer->size() > 0)
            {
                this->reloadTrack();
            }
        }
    }
}

void AutomationTrackMap::onLayerRemoved(const MidiLayer *removedLayer)
{
    if (this->layer)
    {
        if (removedLayer == this->layer)
        {
            this->reloadTrack();
        }
    }
}

void AutomationTrackMap::onProjectBeatRangeChanged(float firstBeat, float lastBeat)
{
    this->projectFirstBeat = firstBeat;
    this->projectLastBeat = lastBeat;
    
    // move first event to the first projects's beat
//    if (AutomationLayer *autoLayer = dynamic_cast<AutomationLayer *>(this->layer.get()))
//    {
//        if (autoLayer->size() > 1)
//        {
//            if (AutomationEvent *event = dynamic_cast<AutomationEvent *>(autoLayer->getUnchecked(0)))
//            {
//                autoLayer->change(*event, event->withParameters(firstBeat, event->getControllerValue()), false);
//            }
//        }
//    }
}


//===----------------------------------------------------------------------===//
// Private
//===----------------------------------------------------------------------===//

void AutomationTrackMap::updateTempoComponent(AutomationEventComponent *component)
{
    component->setBounds(this->getEventBounds(component));
    component->updateConnector();
    component->updateHelper();
}

void AutomationTrackMap::reloadTrack()
{
    for (int i = 0; i < this->eventComponents.size(); ++i)
    {
        this->removeChildComponent(this->eventComponents.getUnchecked(i));
    }
    
    this->eventComponents.clear();
    this->eventsHash.clear();
    
    this->setVisible(false);
    
    for (int j = 0; j < this->layer->size(); ++j)
    {
        MidiEvent *event = this->layer->getUnchecked(j);
        
        if (AutomationEvent *autoEvent = dynamic_cast<AutomationEvent *>(event))
        {
            auto component = new AutomationEventComponent(*this, *autoEvent);
            this->addAndMakeVisible(component);
            
            // update links and connectors
            const int indexOfSorted = this->eventComponents.addSorted(*component, component);
            AutomationEventComponent *previousEventComponent(this->getPreviousEventComponent(indexOfSorted));
            AutomationEventComponent *nextEventComponent(this->getNextEventComponent(indexOfSorted));
            
            component->setNextNeighbour(nextEventComponent);
            //this->updateTempoComponent(component);
            component->toFront(true);
            
            if (previousEventComponent)
            {
                previousEventComponent->setNextNeighbour(component);
            }
            
            this->eventsHash.set(*autoEvent, component);
        }
    }
    
    if (this->eventComponents.size() > 0)
    {
        this->leadingConnector->retargetAndUpdate(nullptr, this->eventComponents[0]);
    }
    
    this->resized();
    this->setVisible(true);
}
