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

#include "HighlightedComponent.h"
#include "ComponentFader.h"

class IconComponent;
class ColourButton;

class ColourButton final : public HighlightedComponent
{
public:

    struct Listener
    {
        virtual ~Listener() {}
        virtual void onColourButtonClicked(ColourButton *button) = 0;
    };

    ColourButton(Colour c, ColourButton::Listener *listener);
    ~ColourButton();

    //[UserMethods]
    void deselect();
    void select();

    bool isSelected() const noexcept;
    Colour getColour() const noexcept;

    int getButtonIndex() const noexcept;
    void setButtonIndex(int val);

    void paint(Graphics &g) override;
    void resized() override;
    void mouseDown(const MouseEvent &e) override;

private:

    Component *createHighlighterComponent() override;

    int index = 0;
    bool selected = false;

    Colour colour;
    UniquePointer<IconComponent> checkMark;
    ColourButton::Listener *owner = nullptr;
    ComponentFader fader;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColourButton)
};
