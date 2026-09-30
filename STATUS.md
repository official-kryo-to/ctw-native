# Current state

The port provides a native Windows gameplay prototype and a separate showcase viewer.
These features are implemented in source; this list is not a claim of complete compatibility with the original.

## World

- City geometry and textures stream around the active camera.
- Water, animated signs, weather colour settings and the day/night cycle render in the city.
- Static light sprites render at night.
- Ground, boxes, cylinders and triangle meshes support collision queries.

## Gameplay

- The player uses the game's layered character sprites and movement animations.
- On-foot movement includes acceleration, gradual turning, ground movement and swept collision.
- The follow camera rotates toward movement and checks buildings for obstruction.
- Character clothing receives daylight shading based on time and light direction.
- Cars and bikes use the vehicle data, models, paint variants and driving physics.
- Vehicle entry and exit use character animations and animated doors.
- Parked-car generators and road traffic populate the city.
- Vehicle collision, damage, smoke, lights, skidmarks and sound effects are implemented.

## Assets and tools

- Local setup reads a user-supplied Android 4.4.243 APK.
- Asset readers handle archives, textures, models, sprites, fonts and text.
- The showcase has texture, text, model, audio and world views.
- Text rendering supports the six game languages. Music and vehicle sound banks are available.
- Screenshot and trace options support local verification.

## Mods

The game loads native plugins through a public C interface. The separate mod kit includes an in-game menu,
texture replacements, time/weather controls and a camera example. The kit builds independently of the game.
