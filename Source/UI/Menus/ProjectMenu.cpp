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
#include "ProjectMenu.h"
#include "ProjectNode.h"
#include "ProjectTimeline.h"
#include "ProjectMetadataActions.h"
#include "ModalDialogInput.h"
#include "ModalDialogConfirmation.h"
#include "PianoTrackNode.h"
#include "AutomationTrackNode.h"
#include "VersionControlNode.h"
#include "KeySignaturesSequence.h"
#include "AudioCore.h"
#include "PianoSequence.h"
#include "HybridRoll.h"
#include "SequencerOperations.h"
#include "MidiTrackActions.h"
#include "PianoTrackActions.h"
#include "AutomationTrackActions.h"
#include "Pattern.h"
#include "UndoStack.h"
#include "MainLayout.h"
#include "Workspace.h"
#include "Config.h"

#define NUM_CONTROLLERS_TO_SHOW 80

ProjectMenu::ProjectMenu(ProjectNode &parentProject, AnimationType animationType) :
    project(parentProject)
{
    this->showMainMenu(animationType);
}

void ProjectMenu::handleCommandMessage(int commandId)
{
    switch (commandId)
    {
        case CommandIDs::ProjectTransposeUp:
        {
            this->project.checkpoint();
            const auto tracks = this->project.getTracks();
            for (int i = 0; i < tracks.size(); ++i)
            {
                if (dynamic_cast<PianoSequence *>(tracks.getUnchecked(i)->getSequence()))
                {
                    tracks.getUnchecked(i)->getPattern()->transposeAll(1, false);
                }
            }
        }
        return;

        case CommandIDs::ProjectTransposeDown:
        {
            this->project.checkpoint();
            const auto tracks = this->project.getTracks();
            for (int i = 0; i < tracks.size(); ++i)
            {
                if (dynamic_cast<PianoSequence *>(tracks.getUnchecked(i)->getSequence()))
                {
                    tracks.getUnchecked(i)->getPattern()->transposeAll(-1, false);
                }
            }
        }
        return;

        case CommandIDs::DeleteProject:
        {
            auto confirmationDialog = ModalDialogConfirmation::Presets::deleteProject();
            auto &project = this->project;
            confirmationDialog->onOk = [&project]()
            {
                auto inputDialog = ModalDialogInput::Presets::deleteProjectConfirmation();
                inputDialog->onOk = [&project](const String &text)
                {
                    if (text == project.getName())
                    {
                        App::Workspace().unloadProject(project.getId(), true, true);
                    }
                    else
                    {
                        App::Layout().showTooltip(TRANS(I18n::Menu::Project::deleteCancelled));
                    }
                };

                App::showModalComponent(move(inputDialog));
            };

            App::showModalComponent(move(confirmationDialog));
            return;
        }

        default:
            break;
    }
}

void ProjectMenu::showMainMenu(AnimationType animationType)
{
    MenuPanel::Menu menu;

    menu.add(MenuItem::item(Icons::piano, CommandIDs::SwitchToEditMode,
        TRANS(I18n::Menu::Project::editorLinear))->closesMenu());

    menu.add(MenuItem::item(Icons::patterns, CommandIDs::SwitchToArrangeMode,
        TRANS(I18n::Menu::Project::editorPattern))->closesMenu());

    menu.add(MenuItem::item(Icons::versionControl, CommandIDs::SwitchToVersioningMode,
        TRANS(I18n::Menu::Project::editorVcs))->closesMenu());

    menu.add(MenuItem::item(Icons::create,
        TRANS(I18n::Menu::Project::addItems))->withSubmenu()->withAction([this]()
        {
            this->showCreateItemsMenu(MenuPanel::SlideLeft);
        }));

    menu.add(MenuItem::item(Icons::render,
        TRANS(I18n::Menu::Project::render))->
        withSubmenu()->
        withAction([this]()
        {
            this->showRenderMenu();
        }));

    menu.add(MenuItem::item(Icons::refactor,
        TRANS(I18n::Menu::Project::refactor))->
        withSubmenu()->
        withAction([this]()
        {
            this->showBatchActionsMenu(MenuPanel::SlideLeft);
        }));
    
    menu.add(MenuItem::item(Icons::close,
        TRANS(I18n::Menu::Project::unload))->
        closesMenu()->
        withAction([this]()
        {
            App::Workspace().unloadProject(this->project.getId(), false, false);
        }));

    menu.add(MenuItem::item(Icons::remove, CommandIDs::DeleteProject, TRANS(I18n::Menu::Project::deleteConfirm)));

    this->updateContent(menu, animationType);
}

void ProjectMenu::showCreateItemsMenu(AnimationType animationType)
{
    MenuPanel::Menu menu;
    menu.add(MenuItem::item(Icons::back,
        TRANS(I18n::Menu::back))->withAction([this]()
        {
            this->showMainMenu(MenuPanel::SlideRight);
        }));

#if PLATFORM_DESKTOP
    menu.add(MenuItem::item(Icons::browse, CommandIDs::ImportMidi,
        TRANS(I18n::Menu::Project::importMidi))->closesMenu());
#endif

    menu.add(MenuItem::item(Icons::pianoTrack,
        TRANS(I18n::Menu::Project::addTrack))->withSubmenu()->withAction([this]()
        {
            this->showNewTrackMenu(MenuPanel::SlideLeft);
        }));

    menu.add(MenuItem::item(Icons::automationTrack,
        TRANS(I18n::Menu::Project::addAutomation))->withSubmenu()->withAction([this]()
        {
            this->showNewAutomationMenu(MenuPanel::SlideLeft);
        }));

    this->updateContent(menu, animationType);
}

void ProjectMenu::showNewTrackMenu(AnimationType animationType)
{
    MenuPanel::Menu menu;
    menu.add(MenuItem::item(Icons::back,
        TRANS(I18n::Menu::back))->withAction([this]()
        {
            this->showCreateItemsMenu(MenuPanel::SlideRight);
        }));

    const auto &instruments = App::Workspace().getAudioCore().getInstruments();
    for (const auto *instrument : instruments)
    {
        const String instrumentId = instrument->getIdAndHash();
        menu.add(MenuItem::item(Icons::instrument,
            instrument->getName())->withAction([this, instrumentId]()
            {
                auto &project = this->project;
                String outTrackId;
                const auto trackTemplate =
                    SequencerOperations::createPianoTrackTemplate(this->project,
                        "", instrumentId, outTrackId);

                auto inputDialog = ModalDialogInput::Presets::newTrack();
                inputDialog->onOk = [trackTemplate, outTrackId, &project](const String &input)
                {
                    project.checkpoint();
                    project.getUndoStack()->perform(new PianoTrackInsertAction(project,
                        &project, trackTemplate, input));

                    // select and auto-zoom the new track for convenience:
                    if (auto *midiTrack = project.findTrackById<PianoTrackNode>(outTrackId))
                    {
                        const auto *clip = midiTrack->getPattern()->getClips().getFirst();
                        project.setEditableScope(midiTrack, *clip, true);
                    }
                };

                App::showModalComponent(move(inputDialog));
            }));
    }

    this->updateContent(menu, animationType);
}

void ProjectMenu::showNewAutomationMenu(AnimationType animationType)
{
    MenuPanel::Menu menu;
    menu.add(MenuItem::item(Icons::back,
        TRANS(I18n::Menu::back))->withAction([this]()
        {
            this->showCreateItemsMenu(MenuPanel::SlideRight);
        }));

    menu.add(MenuItem::item(Icons::automationTrack,
        TRANS(I18n::Menu::Project::addTempo))->
        closesMenu()->
        withAction([this]()
        {
            String outTrackId;
            String instrumentId; // empty, it doesn't matter for master tempo track
            const auto autoTracks = this->project.findChildrenOfType<AutomationTrackNode>();
            const auto autoTrackParams =
                SequencerOperations::createAutoTrackTemplate(this->project,
                    TRANS(I18n::Defaults::tempoTrackName), MidiTrack::tempoController,
                    instrumentId, outTrackId);

            this->project.getUndoStack()->beginNewTransaction();
            this->project.getUndoStack()->perform(new AutomationTrackInsertAction(this->project,
                &this->project, autoTrackParams, TRANS(I18n::Defaults::tempoTrackName)));
        }));

    const auto &instruments = App::Workspace().getAudioCore().getInstruments();
    for (int i = 0; i < instruments.size(); ++i)
    {
        const WeakReference<Instrument> instrument = instruments[i];
        menu.add(MenuItem::item(Icons::instrument,
            instruments[i]->getName())->withSubmenu()->withAction([this, instrument]()
            {
                this->showControllersMenuForInstrument(instrument);
            }));
    }

    this->updateContent(menu, animationType);
}

void ProjectMenu::showControllersMenuForInstrument(const WeakReference<Instrument> instrument)
{
    MenuPanel::Menu menu;
    menu.add(MenuItem::item(Icons::back,
        TRANS(I18n::Menu::back))->withAction([this]()
        {
            this->showNewAutomationMenu(MenuPanel::SlideRight);
        }));

    for (int controllerNumber = 0; controllerNumber < NUM_CONTROLLERS_TO_SHOW; ++controllerNumber)
    {
        const String controllerName = MidiMessage::getControllerName(controllerNumber);
        if (controllerName.isNotEmpty())
        {
            menu.add(MenuItem::item(Icons::automationTrack,
                String(controllerNumber) + ": " + TRANS(controllerName))->
                closesMenu()->
                withAction([this, controllerNumber, instrument]()
                {
                    String outTrackId;
                    const String instrumentId = instrument ? instrument->getIdAndHash() : "";
                    const String trackName = TreeNode::createSafeName(MidiMessage::getControllerName(controllerNumber));
                    const auto autoTrackParams =
                        SequencerOperations::createAutoTrackTemplate(this->project,
                            trackName, controllerNumber, instrumentId, outTrackId);

                    this->project.getUndoStack()->beginNewTransaction();
                    this->project.getUndoStack()->perform(new AutomationTrackInsertAction(this->project,
                        &this->project, autoTrackParams, trackName));
                }));
        }
    }
    
    this->updateContent(menu, MenuPanel::SlideLeft);
}

void ProjectMenu::showRenderMenu()
{
    MenuPanel::Menu menu;

    // TODO! save rendered wavs on mobile in the same way as midi export
#if PLATFORM_DESKTOP
    const bool noRender = false;
#else
    const bool noRender = true;
#endif

    menu.add(MenuItem::item(Icons::back,
        TRANS(I18n::Menu::back))->withAction([this]()
        {
            this->showMainMenu(MenuPanel::SlideRight);
        }));

    menu.add(MenuItem::item(Icons::render, CommandIDs::RenderToWAV,
        TRANS(I18n::Menu::Project::renderWav))->disabledIf(noRender)->closesMenu());

    menu.add(MenuItem::item(Icons::render, CommandIDs::RenderToFLAC,
        TRANS(I18n::Menu::Project::renderFlac))->disabledIf(noRender)->closesMenu());

    menu.add(MenuItem::item(Icons::commit, CommandIDs::ExportMidi,
        TRANS(I18n::Menu::Project::renderMidi))->closesMenu());

    this->updateContent(menu, MenuPanel::SlideLeft);
}

void ProjectMenu::showBatchActionsMenu(AnimationType animationType)
{
    MenuPanel::Menu menu;
    menu.add(MenuItem::item(Icons::back,
        TRANS(I18n::Menu::back))->withAction([this]()
        {
            this->showMainMenu(MenuPanel::SlideRight);
        }));

    menu.add(MenuItem::item(Icons::up,
        CommandIDs::ProjectTransposeUp,
        TRANS(I18n::Menu::Project::transposeUp)));

    menu.add(MenuItem::item(Icons::down,
        CommandIDs::ProjectTransposeDown,
        TRANS(I18n::Menu::Project::transposeDown)));

    //menu.add(MenuItem::item(Icons::group, CommandIDs::RefactorRemoveOverlaps, TRANS(I18n::Menu::Project::cleanup)));

    menu.add(MenuItem::item(Icons::automationTrack, CommandIDs::ProjectSetOneTempo,
        TRANS(I18n::Menu::setOneTempo))->closesMenu());

    const auto &tracks = this->project.findChildrenOfType<MidiTrackNode>();
    const auto &instruments = App::Workspace().getAudioCore().getInstruments();
    if (instruments.size() > 1 && tracks.size() > 0)
    {
        menu.add(MenuItem::item(Icons::instrument,
            TRANS(I18n::Menu::Project::changeInstrument))->withSubmenu()->withAction([this]()
            {
                this->showSetInstrumentMenu();
            }));
    }

    menu.add(MenuItem::item(Icons::refactor,
        TRANS(I18n::Menu::Project::changeTemperament))->withSubmenu()->withAction([this]()
    {
        this->showTemperamentsMenu(false);
    }));

    menu.add(MenuItem::item(Icons::refactor,
        TRANS(I18n::Menu::Project::convertTemperament))->withSubmenu()->withAction([this]()
    {
        this->showTemperamentsMenu(true);
    }));

    this->updateContent(menu, animationType);
}

void ProjectMenu::showTemperamentsMenu(bool convertTracks)
{
    MenuPanel::Menu menu;
    menu.add(MenuItem::item(Icons::back, TRANS(I18n::Menu::back))->withAction([this]()
    {
        this->showBatchActionsMenu(MenuPanel::SlideRight);
    }));

    const auto &temperaments = App::Config().getTemperaments()->getAll();
    const auto currentTemperament = this->project.getProjectInfo()->getTemperament();

    for (const auto otherTemperament : temperaments)
    {
        menu.add(MenuItem::item(Icons::refactor,
            TRANS(otherTemperament->getName()))->
            disabledIf(otherTemperament->getPeriodSize() == currentTemperament->getPeriodSize())->
            closesMenu()->
            withAction([this, currentTemperament, otherTemperament, convertTracks]()
        {
            if (convertTracks)
            {
                const bool hasMadeChanges =
                    SequencerOperations::remapToTemperament(this->project, otherTemperament, true);

                if (!hasMadeChanges)
                {
                    this->project.checkpoint();
                }
            }
            else
            {
                this->project.checkpoint();
            }

            // let's also update key signature if there's only one:
            auto *keySignatures = this->project.getTimeline()->getKeySignatures()->getSequence();
            if (keySignatures->size() == 1)
            {
                auto *firstSignature = static_cast<KeySignatureEvent *>(keySignatures->getUnchecked(0));

                auto newScale = otherTemperament->getHighlighting();
                for (const auto scale : App::Config().getScales()->getAll())
                {
                    if (scale->getBasePeriod() == otherTemperament->getPeriodSize())
                    {
                        newScale = scale;
                        break;
                    }
                }

                const auto rootIndexInChromaticMap = currentTemperament->
                    getChromaticMap()->getNearestScaleKey(firstSignature->getRootKey());

                const auto newRootKey = otherTemperament->getChromaticMap()->
                    getChromaticKey(rootIndexInChromaticMap, 0, true);

                const auto newSignature = firstSignature->withScale(newScale).withRootKey(newRootKey);
                static_cast<KeySignaturesSequence *>(keySignatures)->change(*firstSignature, newSignature, true);
            }

            // finally the temperament itself:
            this->project.getUndoStack()->perform(
                new ProjectTemperamentChangeAction(this->project, *otherTemperament));
        }));
    }

    this->updateContent(menu, MenuPanel::SlideLeft);
}

void ProjectMenu::showSetInstrumentMenu()
{
    MenuPanel::Menu menu;
    menu.add(MenuItem::item(Icons::back, TRANS(I18n::Menu::back))->withAction([this]()
    {
        this->showBatchActionsMenu(MenuPanel::SlideRight);
    }));

    const auto &instruments = App::Workspace().getAudioCore().getInstruments();
    for (const auto *instrument : instruments)
    {
        const String instrumentId = instrument->getIdAndHash();
        menu.add(MenuItem::item(Icons::instrument,
            instrument->getName())->
            closesMenu()->
            withAction([this, instrumentId]()
            {
                DBG(instrumentId);

                const auto tracks = this->project.findChildrenOfType<MidiTrackNode>();

                if (tracks.size() > 0)
                {
                    this->project.getUndoStack()->beginNewTransaction();

                    for (auto *track : tracks)
                    {
                        this->project.getUndoStack()->
                            perform(new MidiTrackChangeInstrumentAction(this->project,
                                track->getTrackId(), instrumentId));
                    }
                }
            }));
    }
    
    this->updateContent(menu, MenuPanel::SlideLeft);
}
