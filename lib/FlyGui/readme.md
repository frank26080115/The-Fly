The GUI backbone code

Start with three 3 classes

 * the gui system itself, named `FlyGui`
 * base class `FlyGuiView`
 * base class `FlyGuiItem`

Implement these in `lib\FlyGui\FlyGui.cpp`, use `lib\FlyGui\FlyGui.h` as the header, there will be many things that inherit from these classes

FlyGuiView and FlyGuiItem are also linked-list nodes

FlyGui must own multiple FlyGuiView in a linked-list

FlyGuiView owns FlyGuiItems

FlyGui must have a public function that will cause it to search through its available views and show the correct one. This means each FlyGuiView must have an unique identifier. There isn't that many views to pick from so there's no need for a fancy search, just iterate until the right one is found.

when the view changes, each FlyGuiView has a on-load and on-unload function to be used. FlyGuiItems also has on-load and on-unload functions, that the FlyGuiView will call on.

FlyGui has a poll function, in it, it handles touch events. The touch event propagates down to the current view, the current view propagates that down to each of its items until one of them says "yes I have handled it" or it misses all of them. FlyGuiItems tracks its own coordinates.

FlyGui polling has a fast mode and slow mode. Fast mode runs with no delays, slow mode runs at 15 Hz, no draws and no checking for touches off-schedule. Slow mode is used when audio is active.

Both the FlyGuiView and FlyGuiItem need to have a dirty flag, and a redraw function that draws itself. This function has a boolean parameter to indicate if the redraw is forced (ignoring dirty flag)

FlyGuiItem tracks it's own origin coordinate as well as its own width and height

FlyGui will have a public function that will call into FlyGuiView to invoke the redrawing, FlyGui will have the logic to dictate frame rate and maybe not draw until the system is less busy.

FlyGuiItems will always have a visible or not visible flag that can be publicly changed, and it sets the dirty flag appropriately

The on-load and on-unload functions are meant for loading PNG files into a RAM buffer, so an item can have an icon. This will be done by whatever inherits FlyGuiItem. Make sure FlyGuiItem can be constructed with an image file path and a main text that can be remembered.

To make sure we don't have multiple items loading the same image into RAM redundantly, the FlyGuiItem can ask its owning FlyGuiView to look at all other sibling items for one that has loaded the same image file, and if it finds one, share the same buffer.

Add another class, that inherits FlyGuiItem, called FlyGuiText. It is constructed with a font size and font style and a maximum buffer length. There is a public function to set the text. If the text changes, the dirty flag is set, but more importantly, the draw function only draws over the characters that has been changed.

There will be a FlyGuiDateTime that inherits FlyGuiText. It always shows the current date and time. Also a FlyGuiStopwatch that inherits FlyGuiText, it knows a starting time and always show the time since the starting time. These two draws might happen often and should be made quick.

FlyGuiModal class is a FlyGuiItem that is owned by FlyGui instead of FlyGuiView, but anything can create one and make FlyGui show it and make FlyGui remove it.

Make FlyGuiText, FlyGuiDateTime, FlyGuiStopwatch, using `lib\FlyGui\FlyGuiText.cpp` and `lib\FlyGui\FlyGuiText.h`. FlyGuiModal can live in `lib\FlyGui\FlyGui.cpp` and `lib\FlyGui\FlyGui.h`

FlyGui is responsible for drawing a top bar with current date and time, and battery status

It will be typical for a FlyGuiView to load a graphic for the bottom bar, with icons aligned with the three dedicated buttons. The FlyGuiView shall have handlers for each of the three press events. The FlyGui polling function is what reads the buttons and then calls the onPressLeft/Mid/Right of the active FlyGuiView.

The FlyGuiViews are:

 * main screen, allows the user to select bluetooth, wifi, memo, or view files
 * bluetooth subscreen, choose which bluetooth device to connect to (skip if only one available)
 * active recording screen, shows status information, and the FlyGuiStopwatch, allows for muting and unmuting and stopping
 * wifi choosing subscreen, choose between AP mode or one of the stored SSIDs
 * web action choosing screen, pick between NTP time sync and one of the available cloud upload destinations
 * AP mode screen, doesn't do anything, the FTP server is running in the background. This uses the bottom buttons to stop
 * Upload progress screen
 * File list viewer

These shall each have its own file inside `lib\FlyGuiViews`
