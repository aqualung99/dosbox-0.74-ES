#ifndef DOSBOX_MIDI_FLUIDSYNTH_H
#define DOSBOX_MIDI_FLUIDSYNTH_H

// This file is based on "midi_mt32.h", part of the MUNT port
//

#include "mixer.h"
#include <fluidsynth.h>

struct SDL_Thread;

class MidiHandler_fluid : public MidiHandler {
public:
	static MidiHandler_fluid &GetInstance(void);

	const char *GetName(void);
	bool Open(const char *conf);
	void Close(void);
	void PlayMsg(Bit8u *msg);
	void PlaySysex(Bit8u *sysex, Bitu len);

private:
	MixerChannel *chan;
	fluid_synth_t *synth;
    fluid_settings_t* pSettings;
	SDL_Thread *thread;
	SDL_mutex *lock;
	SDL_cond *framesInBufferChanged;
	Bit16s *audioBuffer;
	Bitu audioBufferSize;
	Bitu framesPerAudioBuffer;
	Bitu minimumRenderFrames;

	double sampleRateRatio, sampleRate;
	volatile Bitu renderPos, playPos, playedBuffers;
	volatile bool stopProcessing;
	bool open, noise, renderInThread;
    char midiType[4];   // GM, GS, XG, or MMA
    int soundFontID;
    double actualRate;

	static void mixerCallBack(Bitu len);
	static int processingThread(void *);
	static void makeSFPathName(char pathName[], const char romDir[], const char fileName[], bool addPathSeparator);

	MidiHandler_fluid();
	~MidiHandler_fluid();

	Bit32u inline getMidiEventTimestamp() {
		return Bit32u((playedBuffers * framesPerAudioBuffer + (playPos >> 1)) * sampleRateRatio);
	}

	void handleMixerCallBack(Bitu len);
	void renderingLoop();
};

#endif /* DOSBOX_MIDI_FLUID_H */
