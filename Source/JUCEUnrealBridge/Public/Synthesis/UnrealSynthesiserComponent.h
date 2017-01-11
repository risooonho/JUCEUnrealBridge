/*
	JUCE Unreal Bridge 0.0.1
	----
	Sean Soraghan - ROLI
*/

#pragma once

#include "AudioIO/AudioSourceComponent.h"
#include "UnrealWaveVoice.h"
#include "UnrealSynthesiserComponent.generated.h"

UCLASS()
class JUCEUNREALBRIDGE_API UNoteEventInfo : public UObject
{
    GENERATED_BODY()
public:
    UNoteEventInfo(){}
    ~UNoteEventInfo() 
    {
        ClearTimer();
    }

    FORCEINLINE void StartNote (juce::MidiMessageCollector* messageCollector, int midiChannel, int midiNoteNumber, 
                                float onVelocity, float offVelocity, double noteLengthMs)
    {
        MidiCollector     = messageCollector;
        MidiChannel       = midiChannel;
        MidiNote          = midiNoteNumber;
        NoteOnVelocity    = onVelocity;
        NoteOffVelocity   = offVelocity;
        NoteLengthMs      = noteLengthMs;

        juce::MidiMessage m (juce::MidiMessage::noteOn (MidiChannel, MidiNote, NoteOnVelocity));
        m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        MidiCollector->addMessageToQueue (m);
        IsActive = true;
        HasNoteOffScheduled = false;
    }

    FORCEINLINE void ScheduleEndNote (FTimerManager* timerManager)
    {
        if (timerManager != nullptr)
        {
            TimerManager = timerManager;
            TimerManager->ClearTimer (NoteOffHandle);
            TimerManager->SetTimer (NoteOffHandle, this, &UNoteEventInfo::EndNote, NoteLengthMs * 0.001f, false);
            HasNoteOffScheduled = true;
        }
    }

    FORCEINLINE void EndNote()
    {
        if (MidiCollector != nullptr)
        {
            juce::MidiMessage m (juce::MidiMessage::noteOff (MidiChannel, MidiNote, NoteOffVelocity));
            m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
            MidiCollector->addMessageToQueue (m);
        }

        MidiChannel         = 0;
        MidiNote            = 0;
        NoteOnVelocity      = 0.0f;
        NoteOffVelocity     = 0.0f;
        NoteLengthMs        = 0.0;
        IsActive            = false;
        HasNoteOffScheduled = false;
    }

    FORCEINLINE void ClearTimer() 
    {
        if (TimerManager != nullptr)
            TimerManager->ClearTimer (NoteOffHandle);
    }

    FTimerManager* TimerManager = nullptr;

    juce::MidiMessageCollector* MidiCollector;
    UPROPERTY()
    FTimerHandle NoteOffHandle;
    UPROPERTY()
    double NoteLengthMs        = 0.0;
    UPROPERTY()
    float  NoteOnVelocity      = 0.0f;
    UPROPERTY()
    float  NoteOffVelocity     = 0.0f;
    UPROPERTY()
    int    MidiChannel         = 0;
    UPROPERTY()
    int    MidiNote            = 0;
    UPROPERTY()
    bool   IsActive            = false;
    UPROPERTY()
    bool   HasNoteOffScheduled = false;
};

UCLASS()
class JUCEUNREALBRIDGE_API UNoteEventPlayer : public UObject
{
    GENERATED_BODY()
public:
    UNoteEventPlayer()
    {
        ClearNoteOffTimers();
    }

    FORCEINLINE void ClearNoteOffTimers()
    {
        for (int i = 0; i < NoteEvents.Num(); ++i)
            NoteEvents[i]->ClearTimer();
    }

    FORCEINLINE void SetNumberOfNoteSlots (int num)
    {
        ClearNoteOffTimers();
        NoteEvents.Empty();
        for (int i = 0; i < num; ++i)
            NoteEvents.Add (NewObject<UNoteEventInfo> (this));
    }
    
    FORCEINLINE void StartNoteEvent (juce::MidiMessageCollector* messageCollector, int midiChannel, int midiNoteNumber, 
                                     float onVelocity, float offVelocity, double noteTimeMs)
    {
        int availableNoteIndex = FindAvailableNoteIndex();
        if (availableNoteIndex != -1)
        {
            NoteEvents[availableNoteIndex]->StartNote (messageCollector, midiChannel, midiNoteNumber,
                                                      onVelocity, offVelocity, noteTimeMs);
        }
    }

    FORCEINLINE void ScheduleNoteEndsForActiveNotes (FTimerManager* timerManager)
    {
        for (int i = 0; i < NoteEvents.Num(); ++i)
        {
            if (NoteEvents[i]->IsActive && (! NoteEvents[i]->HasNoteOffScheduled))
            {
                NoteEvents[i]->ScheduleEndNote (timerManager);
            }
        }
    }

private:

    UPROPERTY(Transient)
    TArray<UNoteEventInfo*> NoteEvents;

    FORCEINLINE int FindAvailableNoteIndex()
    {
        for (int i = 0; i < NoteEvents.Num(); ++i)
        {
            if ( ! NoteEvents[i]->IsActive)
            {
                return i;
            }
        }
        return -1;
    }
};

//=======================================================================================================
//=======================================================================================================

UCLASS(meta=(BlueprintSpawnableComponent), ClassGroup="JUCE-Components")
class JUCEUNREALBRIDGE_API UUnrealSynthesiserComponent : public UAudioSourceComponent
{
	GENERATED_BODY()
private:
    bool Initialised = false;
public:
	UUnrealSynthesiserComponent()
    {
        bWantsInitializeComponent = true;
        Initialised = false;
        PrimaryComponentTick.bCanEverTick = true;
        NotePlayer = CreateDefaultSubobject<UNoteEventPlayer> (TEXT("SynthNotePlayer"));
    	AssignPrepareToPlayCallback ([this] (int samplesPerBlockExpected, double sampleRate)
    	{
    		PrepareToPlay (samplesPerBlockExpected, sampleRate);
    	});

    	AssignGetNextAudioBlockCallback ([this] (const juce::AudioSourceChannelInfo& bufferToFill)
    	{
    		GetNextAudioBlock (bufferToFill);
    	});
        // Add some voices to our synth, to play the sounds..
        for (int i = 4; --i >= 0;)
        {
            Synth.addVoice (new UnrealWaveVoice());   // These voices will play our custom sine-wave sounds..
        }

        // ..and add a sound for them to play...
        Synth.clearSounds();
        Synth.addSound (new UnrealWaveVoice::UnrealWaveSound());
    }

    FORCEINLINE virtual void InitializeComponent() override
    {
        RegisterComponent();
        NotePlayer->SetNumberOfNoteSlots (20);
    }

    FORCEINLINE virtual void TickComponent (float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override
    {
        if (Initialised)
            NotePlayer->ScheduleNoteEndsForActiveNotes (&GetWorld()->GetTimerManager());
    }

    UFUNCTION(BlueprintCallable, Category = "JUCE-Synthesiser")
    void SetWaveformType (WaveType w)
    {
        if (Initialised)
        {
            for (int i = 0; i < Synth.getNumVoices(); ++i)
            {
                UnrealWaveVoice* voice = (UnrealWaveVoice*) Synth.getVoice (i);
                if (voice != nullptr)
                    voice->SetWaveformType (w);
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Setting parameters on uninitialized synthesiser component. Make sure you call StartAudio() before using the synthesiser (for example during BeginPlay())"));
        }
    }

    UFUNCTION(BlueprintCallable, Category = "JUCE-Synthesiser")
    void SetAttackRateSeconds (float rate) 
    {
        if (Initialised)
        {
            for (int i = 0; i < Synth.getNumVoices(); ++i)
            {
                UnrealWaveVoice* voice = (UnrealWaveVoice*) Synth.getVoice (i);
                if (voice != nullptr)
                    voice->SetAttackRateSeconds ((double) rate);
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Setting parameters on uninitialized synthesiser component. Make sure you call StartAudio() before using the synthesiser (for example during BeginPlay())"));
        }
    }

    UFUNCTION(BlueprintCallable, Category = "JUCE-Synthesiser")
    void SetDecayRateSeconds (float rate) 
    {
        if (Initialised)
        {
            for (int i = 0; i < Synth.getNumVoices(); ++i)
            {
                UnrealWaveVoice* voice = (UnrealWaveVoice*) Synth.getVoice (i);
                if (voice != nullptr)
                    voice->SetDecayRateSeconds ((double) rate);
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Setting parameters on uninitialized synthesiser component. Make sure you call StartAudio() before using the synthesiser (for example during BeginPlay())"));
        }
    }

    UFUNCTION(BlueprintCallable, Category = "JUCE-Synthesiser")
    void SetReleaseRateSeconds (float rate) 
    {
        if (Initialised)
        {
            for (int i = 0; i < Synth.getNumVoices(); ++i)
            {
                UnrealWaveVoice* voice = (UnrealWaveVoice*) Synth.getVoice (i);
                if (voice != nullptr)
                    voice->SetReleaseRateSeconds ((double) rate);
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Setting parameters on uninitialized synthesiser component. Make sure you call StartAudio() before using the synthesiser (for example during BeginPlay())"));
        }
    }

    UFUNCTION(BlueprintCallable, Category = "JUCE-Synthesiser")
    void SetSustainLevel (float level)
    {
        if (Initialised)
        {
            for (int i = 0; i < Synth.getNumVoices(); ++i)
            {
                UnrealWaveVoice* voice = (UnrealWaveVoice*) Synth.getVoice (i);
                if (voice != nullptr)
                    voice->SetSustainLevel ((double) level);
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Setting parameters on uninitialized synthesiser component. Make sure you call StartAudio() before using the synthesiser (for example during BeginPlay())"));
        }
    }

    UFUNCTION(BlueprintCallable, Category = "JUCE-Synthesiser")
    void PlayNoteEvent (int midiChannel, int midiNoteNumber, float onVelocity, float offVelocity, float timeMs)
    {
        if (Initialised)
            NotePlayer->StartNoteEvent (&MidiCollector, midiChannel, midiNoteNumber, onVelocity, offVelocity, timeMs);
        else
            UE_LOG(LogTemp, Warning, TEXT("Setting parameters on uninitialized synthesiser component. Make sure you call StartAudio() before using the synthesiser (for example during BeginPlay())"));
    }

    UFUNCTION(BlueprintCallable, Category = "JUCE-Synthesiser")
    void TriggerNoteOn (int midiChannel, int midiNoteNumber, float velocity)
    {
        if (Initialised)
        {
            juce::MidiMessage m (juce::MidiMessage::noteOn (midiChannel, midiNoteNumber, velocity));
            m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);

            MidiCollector.addMessageToQueue (m);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Setting parameters on uninitialized synthesiser component. Make sure you call StartAudio() before using the synthesiser (for example during BeginPlay())"));
        }
    }

    UFUNCTION(BlueprintCallable, Category = "JUCE-Synthesiser")
    void TriggerNoteOff (int midiChannel, int midiNoteNumber, float velocity)
    {
        if (Initialised)
        {
            juce::MidiMessage m (juce::MidiMessage::noteOff (midiChannel, midiNoteNumber, velocity));
            m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);

            MidiCollector.addMessageToQueue (m);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Setting parameters on uninitialized synthesiser component. Make sure you call StartAudio() before using the synthesiser (for example during BeginPlay())"));
        }
    }
protected:
    FORCEINLINE virtual void PrepareToPlay (int /*samplesPerBlockExpected*/, double sampleRate)
    {
        MidiCollector.reset (sampleRate);

        Synth.setCurrentPlaybackSampleRate (sampleRate);

        Initialised = true;
    }

    FORCEINLINE virtual void GetNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
    {
        // the synth always adds its output to the audio buffer, so we have to clear it
        // first..
        bufferToFill.clearActiveBufferRegion();

        // fill a midi buffer with incoming messages from the midi input.
        juce::MidiBuffer incomingMidi;
        MidiCollector.removeNextBlockOfMessages (incomingMidi, bufferToFill.numSamples);

        // and now get the synth to process the midi events and generate its output.
        Synth.renderNextBlock (*bufferToFill.buffer, incomingMidi, 0, bufferToFill.numSamples);
    }

public:
    //==============================================================================
    // this collects real-time midi messages from the midi input device, and
    // turns them into blocks that we can process in our audio callback
    juce::MidiMessageCollector MidiCollector;

    UPROPERTY()
    UNoteEventPlayer* NotePlayer;

    // the synth itself!
    juce::Synthesiser Synth;

};