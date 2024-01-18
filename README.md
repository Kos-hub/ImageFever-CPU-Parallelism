# ImageFever CPU Parallelism
 
This repository shows my work with CPU parallelism using C++ and an SFML image viewer application. The main goal was to sort a list of images in a directory based on their temperature value. Warmer images should be showed first and cold images would get to the end of the list.

## Parallelisation Technique
The technique used is a thread pool. The idea is to go through the list and assign each thread to one image, so that each image would then be processed in parallel while the app is still showing an either sorted or unsorted list of images. If the next image still hasn't been processed, the app would not stop, it would simply show the unsorted image.

## Future work
With the given images, just parallelising the calculations was enough. However, as also stated in my report, more work can be done if the images have very high resolutions. The image can be split into sections and the thread pool would split the work between various sections of the image. File I/O can also be parallelised, however the range of images in the specifications was small enough to allow the images to be loaded on startup.
