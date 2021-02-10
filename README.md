# vzgpt (Viznut's GPT-2 implementation)

This is my C-only implementation of GPT-2 inference, mostly intended for my
own learning and toying. GPT-2 is the neural language model developed at
OpenAI.

Features:
- CPU-only calculation one token at a time.
- Multithreading supported via pthreads.
- Commandline UI to generate text from a given prompt.
- Terminal and SDL UIs to run the network more interactively.

A video showing off the SDL UI is at
https://www.youtube.com/watch?v=iw2xOVF61tA

In order to run, you need a GPT-2 model dumped into separate raw files under
a single directory. The script dumpckpt.py dumps a model released by OpenAI
into this format. Use the downloader script download_model.py at
https://github.com/openai/gpt-2/ to download them.

Edit config.h to make the hardcoded network parameters match the model you
use. Alternatively, you can define CONSTS_AS_VARS to support all the
different parameter sets with the same executable (but this is still
slightly buggy).

The Makefile uses GCC on a Unix/Linux-type system. If you don't want SDL,
threading etc., you can disable those parts by editing config.h.

Image-GPT models are also technically supported but there's still bughunting
to do before they work properly.

TODO:
- Finish optional INT8 quantization of all the bigger matrices. (yes, I want to be able to run this on small ARM boards and such)
- Finish Image-GPT support.
- Proper configfile.
