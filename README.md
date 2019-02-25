# music-habits

Trying to monitor my speaker/headphone usage on my laptop.

## How does this work?
**Step 1** Gather data to perform analytics.

**Step 2** Perform analytics on the time-series data obtained.

### Step-1 Gather data
Linux machines using ALSA/[Pulseaudio](https://www.freedesktop.org/wiki/Software/PulseAudio/) play sound using a client-server architecture.
A [C program based on pactl](pactl.c) subscribes to events on output devices and applications playing sound, and logs them to a file in CSV-format.

Sample dumps are available in [examples](examples).

### Step-2 Analysis
WIP - gathering data for a week.

## Why pulseaudio?
Pulseaudio provides a great [API](https://freedesktop.org/software/pulseaudio/doxygen/index.html) on Linux which provides
* list of sound sinks (output devices),
* list of sink-inputs (applications/services producing sound),
* properties as volume, corked (is paused), mute, etc. and also
* subscribe to events.

## Why using pactl based C code?
pulseaudio Python drivers are a great choice to use the library. However, I couldn't find one which provides `corked status` for any sink-input. This is required to know that a sink-input (browser/music player) has paused playing, and there is nothing coming out of the speakers.

This is where C came to rescue. However, due to very tiring nature of C, and after dealing with many naive mistakes (coming back from Python/JavaScript/Golang), I went up to tweaking `pactl.c` to my use-case, and it works well.

