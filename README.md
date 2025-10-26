# MattWord
A simple, lightweight, QT based document editor and word processor application. 

This point of this application is to be a small and light weight. At this point most people have used many word processors and document editing applications. We know how they work and we rarely need all of the functionality that they embed. The point of this editor is to simply allow you to create documents, spell check them, insert images, and print them. The documents are saved as an html file. Currently MattWord only uses this format and does not do any conversion. Conversion is on the road map though. 

The executable is under 3 megs and the whole thing runs in most Linux setups under 100 megs of RAM. This isn't a benchmark that has to be maintained. This is just an indication where the project is at this point. 

<img width="800" height="721" alt="image" src="https://github.com/user-attachments/assets/13bf18d4-7ae1-42aa-aa07-957556d5f919" />

MattWord has QT6 for dependencies and if you want spellchecking you will need aspell and aspell-en. This has been tested on Omarchy and Arch Linux. Should work on other distros provided the dependencies can be met. 

Spellchecking will tell you that a word is misspelled but will not suggest the fix or new words. This is on purpose and helps to keep things efficient and quick. 
Image insertion now allows you to decide how large the image should be in terms of width. (200, 300, and 400 pixels) Large images cause the editor to slow down despite the image being scaled to the width the user selects. For now large images should be avoided. 
