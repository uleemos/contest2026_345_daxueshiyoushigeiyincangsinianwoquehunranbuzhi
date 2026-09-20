param([Parameter(Mandatory=$true)][string]$OutputWav)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$fixtureSpeaker = New-Object System.Speech.Synthesis.SpeechSynthesizer
try {
    $fixtureSpeaker.SelectVoice('Microsoft Huihui Desktop')
    $fixtureSpeaker.Rate = 0
    $fixtureFormat = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(
        44100,
        [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
        [System.Speech.AudioFormat.AudioChannel]::Mono)
    $fixtureSpeaker.SetOutputToWaveFile($OutputWav, $fixtureFormat)
    # Unicode escapes keep this script readable by Windows PowerShell 5
    # without depending on its UTF-8/BOM handling.
    $fixtureText = -join ([char[]](0x4f60,0x597d,0xff0c,0x8fd0,0x52a8,
        0x6559,0x7ec3,0x3002,0x4e00,0xff0c,0x4e8c,0xff0c,0x4e09,0x3002))
    $fixtureSpeaker.Speak($fixtureText)
    $fixtureSpeaker.SetOutputToNull()
} finally {
    $fixtureSpeaker.Dispose()
}
Write-Output 'Generated local synthetic speech WAV; no microphone or cloud API used.'
