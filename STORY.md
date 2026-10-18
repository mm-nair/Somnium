## How we built it

We knew from the beginning we wanted to create some sort of camera & submit to the SpaceXAI track. So with Grok Imagine credits, we naturally went to an AI-camera system.

After setting up Cursor & Grokbot, we built the project out in careful steps to avoid the agents getting out of hand.
1. Setting up the basic Somnium utility functions, and adding scripts to quickly flash the ESP32
2. Ensuring all the hardware (buttons, lcd, dial) all worked with the software
3. Writing a UI library for the LCD panel
4. Writing the Python server that managed image outputs
5. Creating the UI, and final end-to-end functionality of the camera

## Challenges we ran into

The ESP32 board we use supports WiFi - unfortunately, the venue couldn't accomodate what we needed. The ESP32 requires 2.4Ghz, which the venue simply didn't have. We attempted to use a phone hot spot, but that prevented our ESP32 from reaching our connected computer, as iPhone's don't allow cross-client talk like that over its network.

We eventually had to settle for USB serial for the time being, which had its own set of problems. After the hardware assembly was finished, either the USB cable or the port was faulty, leading to malformed requests and broken messages being sent. This led to many crashes, failed flashing, and resets of the board. Finally, the issue was narrowed to a couple spots and was (mostly) fixed.

Additionally, the camera appears to have some sort of orientation sensor that changes the way it outputs the image. We never noticed this while testing but it came up after the final assembly. Sometimes we'd reflash the board only to see that our images came out vertical, or that our image was horizontally mirrored. We didn't have enough time to take apart the camera, so instead, we have some final post-processing effects that get applied to ensure the image displays in the right direction.

## Accomplishments that we're proud of

This project took a lot of coordinated effort. Between agents & us, software & hardware, it was genuinely awesome to be able to create a hardware hack. At the end of the day, we accomplished our initial product goal, which is all we needed.

## What we learned

1. With hardware, you need to learn debugging across three areas (software, software-hardware interface, hardware) if you want to avoid wasting time.
2. How to leverage Grok Bot and Cursor to quickly develop applications such as our website.
3. Prompt engineering is crucial for creating good images in models.
4. Frontier models still have their own limitations, don't be afraid to lift the hood and address debugging the way you see fit.
5. Hackathons are a lot of fun, but also a bit stressful.

## What's next for Somnium

First, we need to figure out how to make WiFi work. Whether thats a different board, or a different server-client design is a problem that needs solving. Then, once the device can be disconnected, it needs to be "reconnected" with a proper power source like a battery. This would require a redesign of housing.

Once those fundemental changes are made, it's really just product design from there. Adding more styles, refining the ones we have, investigating how to get our "mood watermark" to show up more subtly, etc.