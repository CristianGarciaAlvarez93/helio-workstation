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

#pragma once

#include "Note.h"
#include "ProjectListener.h"

class HybridRoll;
class ProjectTreeItem;
class TrackMapNoteComponent;

class PianoTrackMap :
    public Component,
    public ProjectListener
{
public:

    PianoTrackMap(ProjectTreeItem &parentProject, HybridRoll &parentRoll);
    ~PianoTrackMap() override;

    //===------------------------------------------------------------------===//
    // Component
    //===------------------------------------------------------------------===//

    void resized() override;

    //===------------------------------------------------------------------===//
    // ProjectListener
    //===------------------------------------------------------------------===//

    void onChangeMidiEvent(const MidiEvent &oldEvent, const MidiEvent &newEvent) override;
    void onAddMidiEvent(const MidiEvent &event) override;
    void onRemoveMidiEvent(const MidiEvent &event) override;

    void onAddTrack(MidiTrack *const track) override;
    void onRemoveTrack(MidiTrack *const track) override;
    void onChangeTrackProperties(MidiTrack *const track) override;
    void onResetTrackContent(MidiTrack *const track) override;

    void onChangeProjectBeatRange(float firstBeat, float lastBeat) override;
    void onChangeViewBeatRange(float firstBeat, float lastBeat) override;

private:

    void applyNoteBounds(TrackMapNoteComponent *nc);
    void reloadTrackMap();

    float projectFirstBeat;
    float projectLastBeat;

    float rollFirstBeat;
    float rollLastBeat;
    
    float componentHeight;
    
    HybridRoll &roll;
    ProjectTreeItem &project;
    
    OwnedArray<TrackMapNoteComponent> eventComponents;
    HashMap<Note, TrackMapNoteComponent *, NoteHashFunction> componentsHashTable;
    
};
