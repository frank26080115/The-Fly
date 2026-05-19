Use Windows style line ending sequences in all file edits.

Use a lot of comments to annotate the code.

If an instruction is not matching any of the design documents, then please point it out. The design documents must always match what the code is doing.

Always avoid using blocking calls, there's timing sensitive audio data being transferred and stored, so performance is an issue.

Do not delete comments unless a change will contradict the comment (or an entire chunk of code is getting deleted and the comment is within the deleted area), in which case, the human must be notified

When coding, style following `.editorconfig` and `.clang-format`

It is acceptable to do a build to check for a specific thing, but if you think the build is about to be tested by the human so that it can be tested on hardware, simply notify the human that it is ready to build and flash.

Do not modify `version.c`, it is auto-generated on every build, do not fight it, simply ignore it.

Try organizing files such that the most important core function are first, then useful helpers, then getters and setters, then debug helpers.
