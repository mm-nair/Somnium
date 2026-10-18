# Somnium

AI art takes 5 steps. Somnium takes 1 click. Photos from the camera are up at [somnium.photo](https://somnium.photo).

## The problem

Getting an AI picture of something you just saw is a chore. On a phone it goes like this:

1. Take the photo
2. Get home
3. Open an AI app
4. Upload it
5. Write a prompt

Somnium is the camera you hand to a friend. They pick a look, press the shutter, and the art comes back on the camera. As easy as a Snapchat filter, except it's a thing you can pass around.

## How we fixed it

The camera is an ESP32-S3 with a small screen, a dial, and a shutter. The dial picks the filter: comic, watercolor, anime, surprise, or pencil sketch. The shutter takes the photo and sends the JPEG to a laptop over USB. The laptop asks Grok to write the edit from that filter, plus a short note about the local light, and Grok Imagine repaints the shot. The result comes back down the same cable onto the screen. The original and the edit both stay on the SD card.

We also put a public gallery up at [somnium.photo](https://somnium.photo), built with Cursor and Grok Bot, so shots from the camera aren't stuck on the device.

The USB frame, the prompt pipeline, and the firmware are written up in the [README](https://github.com/mm-nair/Somnium/blob/main/README.md).

## How we built it

We knew from the beginning we wanted to make a camera and submit to the SpaceXAI track. We had Grok Imagine credits, so an AI camera was the obvious build.

After setting up Cursor and Grok Bot, we built the project in careful steps so the agents didn't get out of hand.

1. Basic Somnium utility functions, and scripts to flash the ESP32 quickly
2. Making sure the buttons, LCD, and dial actually worked with the software
3. A UI library for the LCD panel
4. The Python server that handles image output
5. The on-device UI, and the full path from shutter to edited photo

That server is the photo process. It pulls the weather, turns it into one lighting sentence, has Grok rewrite the style into an edit instruction, and sends the JPEG to Grok Imagine. The picture that comes back is scaled to the screen before it goes back over USB.

## Challenges we ran into

The ESP32 board we use supports WiFi - unfortunately, the venue couldn't accomodate what we needed. The ESP32 requires 2.4Ghz, which the venue simply didn't have. We attempted to use a phone hot spot, but that prevented our ESP32 from reaching our connected computer, as iPhone's don't allow cross-client talk like that over its network.

We eventually had to settle for USB serial for the time being, which had its own set of problems. After the hardware assembly was finished, either the USB cable or the port was faulty, leading to malformed requests and broken messages being sent. This led to many crashes, failed flashing, and resets of the board. Finally, the issue was narrowed to a couple spots and was (mostly) fixed.

Additionally, the camera appears to have some sort of orientation sensor that changes the way it outputs the image. We never noticed this while testing but it came up after the final assembly. Sometimes we'd reflash the board only to see that our images came out vertical, or that our image was horizontally mirrored. We didn't have enough time to take apart the camera, so instead, we have some final post-processing effects that get applied to ensure the image displays in the right direction.

## Accomplishments that we're proud of

This project took a lot of coordinated effort. Between agents & us, software & hardware, it was genuinely awesome to be able to create a hardware hack. At the end of the day, we accomplished our initial product goal, which is all we needed.

## What we learned

1. With hardware, you have to debug across three places (software, the software-hardware boundary, and the hardware) or you waste a lot of time.
2. How to leverage Grok Bot and Cursor to quickly build something, including [somnium.photo](https://somnium.photo).
3. Prompt engineering is crucial if you want good images out of these models.
4. Frontier models still have their own limitations. Don't be afraid to lift the hood and debug it the way you see fit.
5. Hackathons are a lot of fun, and also a bit stressful.

## What's next for Somnium

First, we need to figure out how to make WiFi work. Whether thats a different board, or a different server-client design is a problem that needs solving. Then, once the device can be disconnected, it needs to be "reconnected" with a proper power source like a battery. This would require a redesign of housing.

Once those fundemental changes are made, it's really just product design from there. Adding more styles, refining the ones we have, investigating how to get our "mood watermark" to show up more subtly, etc.