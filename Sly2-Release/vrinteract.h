#pragma once

// (PC, VR first person, 0.1.2: vr.h) The physical interactions' game side, run once per step at step time (game.cpp,
// right after the pad is read: the world is changed only between steps, never from the draw):
//   - pockets: a hand (or the drawn cane's hook) at a guard's readied pocket, then a grip: the prize is taken at once
//     (no reach animation, no prize card), with a buzz; B still steals the game's way (its prize card skipped in VR);
//   - climbing: a hand gripping at a pole, pipe, hook, spire or ledge the game offers (its blue sparkles) grabs it (the
//     game's own Circle); hand over hand up and down a pole; a ledge held while a grip is;
//   - the wristwatch: the current character's owned gadgets listed on it, the one picked put on the gadget button
//     (the left trigger: the game's L1 slot);
//   - the hands' grab zones and the "a pocket is near" state for vr.cpp (no swings, the grips grab).
// Sly2VR.ini (vrprofile.h): pickpocket / climb = grab | button, gadget_menu = watch | button.

struct SW;

void VrStepInteract(SW* psw);
