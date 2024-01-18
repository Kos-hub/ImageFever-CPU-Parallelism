// This is a chopped Pong example from SFML examples

////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <SFML/Graphics.hpp>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>
#include <fstream>
#include "ThreadPool.h"

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>
{
    // Return type of task 'F'
    using return_type = typename std::result_of<F(Args...)>::type;

    // Wrapper for no arguments
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(mtx);

        // don't allow enqueueing after stopping the pool
        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        // Wrapper for no returned value
        tasks.emplace([task]() { (*task)(); });
    }
    cv.notify_one();
    return res;
}

namespace fs = std::filesystem;

// This is a struct named positions that will both hold the current image's hue and its corresponding index
struct Positions {
    float hue;
    int index;
};

// Shared resource between the thread. This will be the sorted list of image positions
std::shared_ptr<std::vector<Positions>> _positions = std::make_shared<std::vector<Positions>>();
std::ofstream threadStream;
// Mutex to lock the resource
std::mutex mtx;
std::condition_variable cv;

// This boolean is used by the main thread to check if the list is currently being sorted
bool sortedList = false;

// This will check if the threadpool is done with the image calculation
bool isDone = false;

/*! \brief Convert RGB to HSV color space

  Converts a given set of RGB values `r', `g', `b' into HSV
  coordinates. The input RGB values are in the range [0, 1], and the
  output HSV values are in the ranges h = [0, 360], and s, v = [0,
  1], respectively.

  \param fR Red component, used as input, range: [0, 1]
  \param fG Green component, used as input, range: [0, 1]
  \param fB Blue component, used as input, range: [0, 1]
  \param fH Hue component, used as output, range: [0, 360]
  \param fS Hue component, used as output, range: [0, 1]
  \param fV Hue component, used as output, range: [0, 1]

*/
void RGBtoHSV(int fR, int fG, int fB, float& fH, float& fS, float& fV) {
    float fCMax = std::max(std::max(fR, fG), fB);
    float fCMin = std::min(std::min(fR, fG), fB);
    float fDelta = fCMax - fCMin;

    if (fDelta > 0) {
        if (fCMax == fR) {
            fH = 60 * (fmod(((fG - fB) / fDelta), 6));
        }
        else if (fCMax == fG) {
            fH = 60 * (((fB - fR) / fDelta) + 2);
        }
        else if (fCMax == fB) {
            fH = 60 * (((fR - fG) / fDelta) + 4);
        }

        if (fCMax > 0) {
            fS = fDelta / fCMax;
        }
        else {
            fS = 0;
        }

        fV = fCMax;
    }
    else {
        fH = 0;
        fS = 0;
        fV = fCMax;
    }

    if (fH < 0) {
        fH = 360 + fH;
    }
}

// This is the task that will be run by the threads. It will calculate the hue, create a Positions object that will then be used to sort the list
void CalculateHue(std::string fileName, int index)
{
    // Profiling.
    std::string threadTotalTime;
    auto startThread = std::chrono::high_resolution_clock::now();

    // Initialising Texture and Positions
    sf::Texture texture;
    Positions pos;

    // Check if we can retrieve the texture
    if (!texture.loadFromFile(fileName))
        return;

    // Copying to image to get the pixels
    auto image = texture.copyToImage();

    // Hue Calculation
    std::vector<float> hue;

    for (int i = 0; i < image.getSize().x * image.getSize().y; i++)
    {
        float h = 0.f;
        float s = 0.f;
        float v = 0.f;

        float x = i % image.getSize().x;
        float y = i / image.getSize().x;


        auto pixel = image.getPixel(x, y);

        RGBtoHSV(pixel.r, pixel.g, pixel.b, h, s, v);
        hue.push_back(h);
    }

    // Sorting and getting the hue to get the temperature of the picture
    std::sort(hue.begin(), hue.end());

    float median;
    if (hue.size() % 2 == 0)
    {
        median = (hue[hue.size() / 2 - 1] + hue[hue.size() / 2]) / 2;
    }
    else
    {
        median = hue[hue.size() / 2];
    }

    // Getting the decimal part of the median.
    float intpart = 0.f;
    float frac = modf((median / 360.f) + 1.f / 6.f, &intpart);

    // Setting the positions value
    pos.hue = frac;
    pos.index = index;

    {
        // Locking the list so that multiple threads will not be able to write to the same resource
        std::unique_lock lock(mtx);
        _positions->push_back(pos);
        // Each thread will sort the list every time it writes something to the list
        std::sort(_positions->begin(), _positions->end(), [](const auto& a, const auto& b) { return a.hue < b.hue; });

        // Setting the flag to true, letting main thread know it is possible to read the shared resource
        sortedList = true;

        // Profiling
        auto endThread = std::chrono::high_resolution_clock::now();

        auto totalTime = endThread - startThread;

        threadStream << std::chrono::duration_cast<std::chrono::milliseconds>(totalTime).count() << std::endl;
    }



}

sf::Vector2f ScaleFromDimensions(const sf::Vector2u& textureSize, int screenWidth, int screenHeight)
{
    float scaleX = screenWidth / float(textureSize.x);
    float scaleY = screenHeight / float(textureSize.y);
    float scale = std::min(scaleX, scaleY);
    return { scale, scale };
}

void CalcHue(std::vector<std::string> imageFileNames)
{
    std::string threadTotalTime;
    auto startThread = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < imageFileNames.size(); i++)
    {


        // Initialising Texture and Positions
        sf::Texture texture;
        Positions pos;

        // Check if we can retrieve the texture
        if (!texture.loadFromFile(imageFileNames[i]))
            return;

        // Copying to image to get the pixels
        auto image = texture.copyToImage();

        // Hue Calculation
        std::vector<float> hue;

        for (int i = 0; i < image.getSize().x * image.getSize().y; i++)
        {
            float h = 0.f;
            float s = 0.f;
            float v = 0.f;

            float x = i % image.getSize().x;
            float y = i / image.getSize().x;


            auto pixel = image.getPixel(x, y);

            RGBtoHSV(pixel.r, pixel.g, pixel.b, h, s, v);
            hue.push_back(h);
        }

        // Sorting and getting the hue to get the temperature of the picture
        std::sort(hue.begin(), hue.end());

        float median;
        if (hue.size() % 2 == 0)
        {
            median = (hue[hue.size() / 2 - 1] + hue[hue.size() / 2]) / 2;
        }
        else
        {
            median = hue[hue.size() / 2];
        }

        // Getting the decimal part of the median.
        float intpart = 0.f;
        float frac = modf((median / 360.f) + 1.f / 6.f, &intpart);

        // Setting the positions value
        pos.hue = frac;
        pos.index = i;

        {
            // Locking the list so that multiple threads will not be able to write to the same resource
            std::unique_lock lock(mtx);
            _positions->push_back(pos);
            // Each thread will sort the list every time it writes something to the list
            std::sort(_positions->begin(), _positions->end(), [](const auto& a, const auto& b) { return a.hue < b.hue; });

            // Setting the flag to true, letting main thread know it is possible to read the shared resource
            sortedList = true;


        }


    }

    // Profiling
    auto endThread = std::chrono::high_resolution_clock::now();

    auto totalTime = endThread - startThread;

    threadStream << std::chrono::duration_cast<std::chrono::milliseconds>(totalTime).count() << std::endl;
}

int main()
{
   
    //std::srand(static_cast<unsigned int>(std::time(NULL)));

    // Total Sorting CSV
    std::ofstream totalSort("TotalSort.csv", std::ofstream::out);
    std::string totalSortTime;

    // Thread CSV
    threadStream = std::ofstream("ThreadTime.csv", std::ofstream::out);

    // Loading Time CSV
    std::ofstream loadingTime("LoadingTime.csv", std::ofstream::out);
    std::string totalLoadingTime;


    auto startLoad = std::chrono::high_resolution_clock::now();
    // example folder to load images
    const char* image_folder = "./unsorted";
    std::vector<std::string> imageFilenames;
    for (auto& p : fs::directory_iterator(image_folder))
    {
        imageFilenames.push_back(p.path().u8string());
    }
    

    // Initialising the thread pool
    const auto num_threads = std::thread::hardware_concurrency();
    ThreadPool threadpool(num_threads);

    // Profiling thread pool
    auto startPool = std::chrono::high_resolution_clock::now();

    //std::thread thread(CalcHue, imageFilenames);

    // The number of tasks will correspond to the number of images in the folder.
    for (int i = 0; i < imageFilenames.size(); i++)
    {
        threadpool.enqueue(CalculateHue, imageFilenames[i], i);
    }


    // Define some constants

    const int gameWidth = 800;
    const int gameHeight = 600;

    int imageIndex = 0;

    // Create the window of the application
    sf::RenderWindow window(sf::VideoMode(gameWidth, gameHeight, 32), "Image Fever",
                            sf::Style::Titlebar | sf::Style::Close);
    window.setVerticalSyncEnabled(true);



    // Load an image to begin with
    sf::Texture texture;
    if (!texture.loadFromFile(imageFilenames[0]))
        return EXIT_FAILURE;
    sf::Sprite sprite(texture);
    // Make sure the texture fits the screen
    sprite.setScale(ScaleFromDimensions(texture.getSize(), gameWidth, gameHeight));

    auto endLoad = std::chrono::high_resolution_clock::now();
    auto totalLoad = endLoad - startLoad;

    totalLoadingTime.append(std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(totalLoad).count()));

    while (window.isOpen())
    {
        // Profiling the entire pool to see how well it performs
        if (_positions->size() == imageFilenames.size() && !isDone)
        {
            auto endPool = std::chrono::high_resolution_clock::now();

            auto total = endPool - startPool;

            totalSortTime.append(std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(total).count()));

            isDone = true;
        }

        // Handle events
        sf::Event event;
        while (window.pollEvent(event))
        {

            // Window closed or escape key pressed: exit
            if ((event.type == sf::Event::Closed) ||
               ((event.type == sf::Event::KeyPressed) && (event.key.code == sf::Keyboard::Escape)))
            {
                totalSort << totalSortTime;
                loadingTime << totalLoadingTime;
                window.close();
                break;
            }
            
            // Window size changed, adjust view appropriately
            if (event.type == sf::Event::Resized)
            {
                sf::View view;
                view.setSize(gameWidth, gameHeight);
                view.setCenter(gameWidth/2.f, gameHeight/2.f);
                window.setView(view);
            }

            // Arrow key handling!
            if (event.type == sf::Event::KeyPressed)
            {
                // adjust the image index
                if (event.key.code == sf::Keyboard::Key::Left)
                    imageIndex = (imageIndex + imageFilenames.size() - 1) % imageFilenames.size();
                else if (event.key.code == sf::Keyboard::Key::Right)
                    imageIndex = (imageIndex + 1) % imageFilenames.size();

                std::string imageFilename;


                // get image filename

                // Firstly we check if the threads are accessing the shared resource
                if (sortedList)
                {
                    // If the main thread is currently trying to access an index that isn't sorted yet, we just get the unsorted index
                    if (_positions->size() <= imageIndex)
                    {
                        imageFilename = imageFilenames[imageIndex];
                    }     
                    else
                    {
                        // If the main thread can access it then we lock the resource and read from it
                        std::unique_lock lock(mtx);
                        imageFilename = imageFilenames[(*_positions)[imageIndex].index];

                    }
                }
                else
                {
                    // If the other threads are working with the shared resource, main will just get the unsorted index
                    imageFilename = imageFilenames[imageIndex];
                }
                
               
                // set it as the window title 
                window.setTitle(imageFilename);
                // ... and load the appropriate texture, and put it in the sprite
                if (texture.loadFromFile(imageFilename))
                {
                    sprite = sf::Sprite(texture);
                    sprite.setScale(ScaleFromDimensions(texture.getSize(), gameWidth, gameHeight));
                }
            }
        }

        // Clear the window
        window.clear(sf::Color(0, 0, 0));
        // draw the sprite
        window.draw(sprite);
        // Display things on screen
        window.display();
    }

    totalSort.close();
    loadingTime.close();
    threadStream.close();

    //thread.join();
    return EXIT_SUCCESS;
}
