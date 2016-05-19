#include <SDL_thread.h>
#include <SDL_endian.h>
#include "control.h"

#ifndef DOSBOX_MIDI_H
#include "midi.h"
#endif

#include "midi_fluid.h"

MidiHandler_fluid &MidiHandler_fluid::GetInstance() {
	static MidiHandler_fluid midiHandler_fluid;
	return midiHandler_fluid;
}

const char *MidiHandler_fluid::GetName(void) {
	return "fluidsynth";
}

bool MidiHandler_fluid::Open(const char *conf) {
	Section_prop *section = static_cast<Section_prop *>(control->GetSection("midi"));
	const char *sfFile = section->Get_string("fluidsynth.soundFontFile");

    fluid_settings_t* pSettings = new_fluid_settings();

    fluid_settings_setnum(pSettings, "synth.sample-rate", (double)section->Get_int("fluidsynth.sampleRate"));

    actualRate = 0.0;
    fluid_settings_getnum(pSettings, "synth.sample-rate", &actualRate);
    LOG_MSG("FluidSynth: Synthesizer running at a sample rate of %d", (int)actualRate);

    const char *szMidiType = section->Get_string("fluidsynth.midiType");
    fluid_settings_setstr(pSettings, "synth.midi-bank-select", szMidiType);

    fluid_settings_setint(pSettings, "synth.cpu-cores", 1 + section->Get_bool("fluidsynth.thread") ? 1 : 0);

    synth = new_fluid_synth(pSettings);

    soundFontID = fluid_synth_sfload(synth, sfFile, 1);
    if (soundFontID == -1)
    {
        delete_fluid_synth(synth);
        delete_fluid_settings(pSettings);
        LOG_MSG("FluidSynth: Could not open SoundFont file \"%s\"! FluidSynth disabled!", sfFile);
        return false;
    }

    fluid_synth_set_interp_method(synth, -1, section->Get_int("fluidsynth.interpolation"));

//	if (noise) LOG_MSG("FluidSynth: Set maximum number of partials %d", synth->getPartialCount());
//	if (noise) LOG_MSG("MT32: Adding mixer channel at sample rate %d", synth->getStereoOutputSampleRate());

	chan = MIXER_AddChannel(mixerCallBack, actualRate, "FluidSynth");

	if (renderInThread) {
/*
		stopProcessing = false;
		playPos = 0;
		sampleRateRatio = 1.0f;
		int chunkSize = section->Get_int("fluidsynth.chunk");
		minimumRenderFrames = (chunkSize * actualRate) / MILLIS_PER_SECOND;
		int latency = section->Get_int("fluidsynth.prebuffer");
		if (latency <= chunkSize) {
			latency = 2 * chunkSize;
			LOG_MSG("FluidSynth: chunk length must be less than prebuffer length, prebuffer length reset to %i ms.", latency);
		}
		framesPerAudioBuffer = (latency * actualRate) / MILLIS_PER_SECOND;
		audioBufferSize = framesPerAudioBuffer << 1;
		audioBuffer = new Bit16s[audioBufferSize];
		synth->render(audioBuffer, framesPerAudioBuffer - 1);
		renderPos = (framesPerAudioBuffer - 1) << 1;
		playedBuffers = 1;
		lock = SDL_CreateMutex();
		framesInBufferChanged = SDL_CreateCond();
		thread = SDL_CreateThread(processingThread, "FluidSynth", NULL);
*/
	}
	chan->Enable(true);

	open = true;
	return true;
}

void MidiHandler_fluid::Close(void) {
	if (!open) return;
	chan->Enable(false);
	if (renderInThread) {
/*
		stopProcessing = true;
		SDL_LockMutex(lock);
		SDL_CondSignal(framesInBufferChanged);
		SDL_UnlockMutex(lock);
		SDL_WaitThread(thread, NULL);
		thread = NULL;
		SDL_DestroyMutex(lock);
		lock = NULL;
		SDL_DestroyCond(framesInBufferChanged);
		framesInBufferChanged = NULL;
		delete[] audioBuffer;
		audioBuffer = NULL;
*/
	}
	MIXER_DelChannel(chan);
	chan = NULL;

    delete_fluid_synth(synth);
    delete_fluid_settings(pSettings);

	synth = NULL;
	open = false;
}

void MidiHandler_fluid::PlayMsg(Bit8u *msg) {
    if (renderInThread) {
    //		synth->playMsg(SDL_SwapLE32(*(Bit32u *)msg), getMidiEventTimestamp());
    } else {
        Bit32u endianCorrectMsg = SDL_SwapLE32(*(Bit32u *)msg);
        unsigned char *msgBytes = (unsigned char *)&endianCorrectMsg;
        unsigned char msgType = (msgBytes[0] & 0xF0) >> 4;
        unsigned char channelNum = (msgBytes[0] & 0x0F);
        unsigned char d1Data = (msgBytes[1] & 0x7F);
        unsigned char d2Data = (msgBytes[2] & 0x7F);

        switch(msgType)
        {
          case 0x08:
          {
            fluid_synth_noteoff(synth, channelNum, d1Data);
            break;
          }

          case 0x09:
          {
            fluid_synth_noteon(synth, channelNum, d1Data, d2Data);
            break;
          }

            case 0x0A:
            {
                // FluidSynth has nothing for us to call
                // to handle this message. So, just ignore it!
                //
                break;
            }

          case 0x0B:
          {
            fluid_synth_cc(synth, channelNum, d1Data, d2Data);
            break;
          }

          case 0x0C:
          {
            fluid_synth_program_change(synth, channelNum, d1Data);
            break;
          }

          case 0x0D:
          {
            fluid_synth_channel_pressure(synth, channelNum, d1Data);
            break;
          }

          case 0x0E:
          {
            unsigned pitchBendValue = (((unsigned)d2Data) << 7) | (unsigned)d1Data;
            fluid_synth_pitch_bend(synth, channelNum, pitchBendValue);
            break;
          }
        }
	}
}

void MidiHandler_fluid::PlaySysex(Bit8u *sysex, Bitu len) {
	if (renderInThread) {
//		synth->playSysex(sysex, len, getMidiEventTimestamp());
	} else {
        char responseBuffer[4 * 1024] = { '\0' };
        int bufferLen = 4 * 1024;
        int handled = 0;

        if (fluid_synth_sysex(synth, (char*)sysex, len, responseBuffer, &bufferLen, NULL, 0) == FLUID_OK)
        {
            if (responseBuffer[0] && bufferLen > 0)
            {
                LOG_MSG("FluidSynth: sysex message returned \"%s\"", responseBuffer);
            }
        }
        else
        {
            LOG_MSG("FluidSynth: sysex message failed");
        }

//		synth->playSysex(sysex, len);
	}
}

void MidiHandler_fluid::mixerCallBack(Bitu len) {
	MidiHandler_fluid::GetInstance().handleMixerCallBack(len);
}

int MidiHandler_fluid::processingThread(void *) {
	MidiHandler_fluid::GetInstance().renderingLoop();
	return 0;
}

MidiHandler_fluid::MidiHandler_fluid() : open(false), chan(NULL), synth(NULL), thread(NULL) {
}

MidiHandler_fluid::~MidiHandler_fluid() {
	Close();
}

void MidiHandler_fluid::handleMixerCallBack(Bitu len) {
	if (renderInThread) {
	/*
		while (renderPos == playPos) {
			SDL_LockMutex(lock);
			SDL_CondWait(framesInBufferChanged, lock);
			SDL_UnlockMutex(lock);
			if (stopProcessing) return;
		}
		Bitu renderPosSnap = renderPos;
		Bitu playPosSnap = playPos;
		Bitu samplesReady = (renderPosSnap < playPosSnap) ? audioBufferSize - playPosSnap : renderPosSnap - playPosSnap;
		if (len > (samplesReady >> 1)) {
			len = samplesReady >> 1;
		}
		chan->AddSamples_s16(len, audioBuffer + playPosSnap);
		playPosSnap += (len << 1);
		while (audioBufferSize <= playPosSnap) {
			playPosSnap -= audioBufferSize;
			playedBuffers++;
		}
		playPos = playPosSnap;
		renderPosSnap = renderPos;
		const Bitu samplesFree = (renderPosSnap < playPosSnap) ? playPosSnap - renderPosSnap : audioBufferSize + playPosSnap - renderPosSnap;
		if (minimumRenderFrames <= (samplesFree >> 1)) {
			SDL_LockMutex(lock);
			SDL_CondSignal(framesInBufferChanged);
			SDL_UnlockMutex(lock);
		}
		*/
	} else {
        int result = fluid_synth_write_s16(synth, len, MixTemp, 0, 2, MixTemp, 1, 2);
		chan->AddSamples_s16(len, (Bit16s *)MixTemp);
	}
}

void MidiHandler_fluid::renderingLoop() {
/*
	while (!stopProcessing) {
		const Bitu renderPosSnap = renderPos;
		const Bitu playPosSnap = playPos;
		Bitu samplesToRender;
		if (renderPosSnap < playPosSnap) {
			samplesToRender = playPosSnap - renderPosSnap - 2;
		} else {
			samplesToRender = audioBufferSize - renderPosSnap;
			if (playPosSnap == 0) samplesToRender -= 2;
		}
		Bitu framesToRender = samplesToRender >> 1;
		if ((framesToRender == 0) || ((framesToRender < minimumRenderFrames) && (renderPosSnap < playPosSnap))) {
			SDL_LockMutex(lock);
			SDL_CondWait(framesInBufferChanged, lock);
			SDL_UnlockMutex(lock);
		} else {
			synth->render(audioBuffer + renderPosSnap, framesToRender);
			renderPos = (renderPosSnap + samplesToRender) % audioBufferSize;
			if (renderPosSnap == playPos) {
				SDL_LockMutex(lock);
				SDL_CondSignal(framesInBufferChanged);
				SDL_UnlockMutex(lock);
			}
		}
	}
	*/
}
