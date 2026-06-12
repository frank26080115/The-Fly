For a local network workflow, you need:

 * The Fly, connected via Wi-Fi to your local network
 * a PC, connected to the same local network
 * Software running on this PC, which is the subject of this document

# Installation

## Python Install

Please download and install Python version `3.11.7`, or later, and make sure it is available on the system path.

Please download a copy of the `scripts` directory of this project repository and then place it in a location of your choice.

There is a `scripts\requirements.txt` file, which contains a list of python libraries that are required. You can use the command `python -m pip install -r tools\requirements.txt` to install them all automatically.

## Local AI Transcription and Summarization

Local AI can be used to perform transcription and summarization.

The `scripts\requirements.txt` already contains the libraries needed to use `faster-whisper` and `openai-whisper`, both are local transcription AI.

For other models, Ollama is used. Please install [Ollama from https://ollama.com/](https://ollama.com/) first. Make sure it is accessible from the system path.

After installation, run the commands similar to

```
ollama pull qwen3:14b
ollama pull gemma3:4b
ollama pull llama3.3:70b
ollama pull deepseek-r1:8b
```

The above commands will download and install the specified models for usage with Ollama. These models are very large in size (hundreds of MB or even multiple GB), you can pick just the one you want to use, you don't need all of them.

# Usage

Connect The Fly to your Wi-Fi local network. [See the document about Wi-Fi Operations](user-manual-wifi-operations.md) for details.

Using the command line terminal, navigate to the directory containing the `scripts`.

Run the command: `python thefly_desktop.py`

This will:

 * Automatically find The Fly on your local network and connect to it
 * Query it for a list of available files
 * Download the files (only the new ones)
 * Transcribe the files
 * Summarize the files

This will likely take a long time

## Default AI Models

Currently, the script defaults to using a minimal `faster-whisper` executed on the CPU for transcription, and `qwen3:14b` for the summarization.

## Online AI Models

The script has many optional user specified arguments that can be used, calling the script like

`python thefly_desktop.py --transcription-model gpt-4o-transcribe-diarize --summary-model gpt-4o-mini`

Will tell it to use `gpt-4o-transcribe-diarize` for the transcription and `gpt-4o-mini` for the summarization

To use OpenAI API models, you must have your own OpenAI API key, and place it into your system environment variables as `OPENAI_API_KEY`

## Advanced Usage

The `thefly_desktop.py` script supports these optional command line arguments:

| Argument | Description |
| --- | --- |
| `-h`, `--help` | Show the command line help text and exit. |
| `--device DEVICE`, `--url DEVICE` | The-Fly device IP, hostname, or base URL. If omitted, the script tries to find The Fly by mDNS. |
| `--db-dir DB_DIR` | Database directory. Defaults to `the-fly` in the current user's home directory. |
| `--clean` | Delete device files after a local audio copy exists. |
| `--transcribe` | Transcribe and summarize audio files downloaded in this session. |
| `--transcribe-file TRANSCRIBE_FILE` | Process one local `.rec`, `.fly`, `.wav`, `.mp3`, or `.json` file. This argument can be used more than once. |
| `--transcribe-all` | Process missing transcriptions and summaries for local `audio/*.wav` and `audio/*.mp3` files. |
| `--transcription-model TRANSCRIPTION_MODEL` | Transcription model. Leave blank to use `faster-whisper;small;cpu;int8`. |
| `--summary-model SUMMARY_MODEL` | Summarization model. Leave blank to use `qwen3:14b`. |
| `--force-transcribe` | Overwrite existing transcription and summary outputs. |
| `--device-timeout DEVICE_TIMEOUT` | Device HTTP timeout in seconds. |
| `--mdns-timeout MDNS_TIMEOUT` | mDNS discovery timeout in seconds. |
| `--api-timeout API_TIMEOUT` | AI API timeout in seconds. |
| `--max-output-tokens MAX_OUTPUT_TOKENS` | Maximum number of output tokens for summaries. |
| `--gap-threshold-ms GAP_THRESHOLD_MS` | Gap threshold in milliseconds used while decoding audio. |
| `--ping` | Discover and ping The Fly, then exit without file operations. |
| `--key KEY` | Filecrypt key file for encrypted `.rec` and `.fly` files. (only for firmware with security features) |
| `--password PASSWORD` | Derive the filecrypt key from this password for this session. (only for firmware with security features) |

Hint: you can create a wrapper script that always calls the python script with your favorite arguments.

# Other Custom Software Workflows

The firmware implementation of The Fly makes it easy for other automated workflows to be implemented. The files can be downloaded via HTTP or via FTP.

To obtain a list of files on the device, download from the URL `/list_files.json`

To download a particular file, download from the URL `/download_file?file_name=<file name here>`

To delete a particular file, send a request to the URL `/delete_file?file_name=<file name here>`

On firmware without security features, FTP access (plain FTP, port 22, not SFTP) is possible, the username and password are both "thefly".
