# MacOS

## Purpose

This guide explains how to correctly configure **macOS** to allow **Cool Retro Term.app** (CRT) to capture **system audio** — i.e., the sound your computer is playing — instead of microphone noise.  
It uses the free **BlackHole** virtual audio driver and a **Multi-Output Device** so that audio can be heard through your speakers while being routed into the visualizer.

> **Note:** The steps involving application modification (re-signing, etc.) are specific workarounds for `cool-retro-term`. For standard terminals like Terminal.app or iTerm2, you only need to follow the BlackHole setup described in the main `README.md`.

> **Important Note:** In Sound Settings, after you change to `Multi-Output Device`, you will **NOT** be able to adjust the volume, switch it back to `default` when you are not using the who app. Very important ah this one.

---

## Step 1. Install BlackHole

BlackHole is a virtual audio driver that lets applications record system audio.

### Installation via Homebrew

```bash
brew install --cask blackhole-2ch
```

This installs a 2-channel version, sufficient for stereo system audio.

---

## Step 2. Create a Multi-Output Device (Audio + Capture)

1. Open **Audio MIDI Setup** (press **⌘ + Space**, type `Audio MIDI Setup`, and hit Enter).
2. Click the **“＋”** button in the lower-left corner.
3. Select **Create Multi-Output Device**.
4. In the right panel, check:
   - ✅ **BlackHole 2ch**
   - ✅ **Your speakers/headphones** (e.g. “MacBook Pro Speakers” or “External Headphones”)
5. Set the **Clock Source** to your **physical output device** (not BlackHole).
   - Drag it to the top of the list or use the dropdown.
6. (Optional) Rename the Multi-Output Device to something meaningful, e.g. **“BlackHole Mix.”**
7. Right-click the device → **Use This Device for Sound Output.**

✅ You now hear your normal audio, and BlackHole receives the same signal silently in the background.

---

## Step 3. Set BlackHole as Input (for Apps to Capture)

Open **System Settings → Sound → Input**  
Select **BlackHole 2ch** as the **Input Device**.

macOS will now treat your system audio as the microphone input.  
Apps that access the mic will receive whatever your system is playing.

---

## Step 4. Enable Cool Retro Term for Audio Access

By default, macOS blocks mic (and therefore system-audio) access for third-party apps.  
We need to let CRT request that permission.

### 4.1. Make a Local Copy

```bash
cp -R "/Applications/cool-retro-term.app" ~/Applications/
```

Always work on your own copy to avoid system integrity issues.

### 4.2. Add Mic Usage Description

```bash
plutil -insert NSMicrophoneUsageDescription -string "Needed to capture system audio for visualization." "~/Applications/cool-retro-term.app/Contents/Info.plist"
```

### 4.3. Clear Quarantine (optional)

```bash
xattr -dr com.apple.quarantine "~/Applications/cool-retro-term.app"
```

### 4.4. Re-sign the App

This ensures macOS accepts the modified bundle.

```bash
codesign --force --deep --sign - "~/Applications/cool-retro-term.app"
```

### 4.5. Reset Mic Permissions (optional)

```bash
tccutil reset Microphone
```

### 4.6. Launch the Local Copy

Run `~/Applications/cool-retro-term.app` directly.  
macOS should now prompt:

> “Cool Retro Term would like to access the microphone.”

Click **Allow**.

You’ll now be able to capture the BlackHole input inside CRT.

---

## Step 5. Verify

### 5.1. Check Audio Flow

- In **Audio MIDI Setup → Input**, BlackHole’s meter should bounce when playing system audio.
- In **System Settings → Sound → Input**, BlackHole 2ch should show level changes.

### 5.2. Check Permissions

Go to **System Settings → Privacy & Security → Microphone**  
Ensure **Cool Retro Term** is toggled **On**.

### 5.3. Run Your App

```bash
./build/who --system
```

Your visualizer should now respond to system audio.

---

## Step 6. Troubleshooting

| Issue                            | Cause                          | Solution                               |
| -------------------------------- | ------------------------------ | -------------------------------------- |
| No sound in speakers             | Clock source set to BlackHole  | Change clock source to physical output |
| No permission prompt             | App not re-signed or corrupted | Reinstall CRT and repeat Step 4        |
| Still silent capture             | Wrong input selected           | Choose BlackHole 2ch in Sound → Input  |
| `ls` or commands fail inside CRT | Signature invalid              | Re-sign using `codesign --sign -`      |
| “no identity found” error        | Typo in command                | Ensure space: `--sign -`               |

---

## Step 7. Optional Checks

### List Devices (for debugging)

```bash
system_profiler SPAudioDataType
```

or with miniaudio code:

```bash
who --list-devices
```

### Verify Signature

```bash
codesign -dv --verbose=4 "~/Applications/cool-retro-term.app"
```

Look for `Authority=Ad Hoc Signature`.

---

## Summary

✅ You can now run **Cool Retro Term.app** and capture **system audio** using **BlackHole** + **Multi-Output Device**, without environmental noise.  
**Flow:**

```
Audio Player → Multi-Output (BlackHole + Speakers) → BlackHole Input → Cool Retro Term → who
```

Once configured, this setup is stable and reusable across sessions.
