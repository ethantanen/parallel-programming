#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdint>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <time.h>
#include <omp.h>

using namespace cv;

// Represents a pixel as a triple of intensities
struct pixel {
    double red;
    double green;
    double blue;
    
    pixel(double r, double g, double b) : red(r), green(g), blue(b) {};
};

/*
 * The Prewitt kernels can be applied after a blur to help highlight edges
 * The input image must be gray scale/intensities:
 *     double intensity = (in[in_offset].red + in[in_offset].green + in[in_offset].blue)/3.0;
 * Each kernel must be applied to the blured images separately and then composed:
 *     blurred[i] with prewittX -> Xedges[i]
 *     blurred[i] with prewittY -> Yedges[i]
 *     outIntensity[i] = sqrt(Xedges[i]*Xedges[i] + Yedges[i]*Yedges[i])
 * To turn the out intensity to an out color set each color to the intensity
 *     out[i].red = outIntensity[i]
 *     out[i].green = outIntensity[i]
 *     out[i].blue = outIntensity[i]
 *
 * For more on the Prewitt kernels and edge detection:
 *     http://en.wikipedia.org/wiki/Prewitt_operator
 */
void prewittX_kernel(const int rows, const int cols, double * const kernel) {
    if(rows != 3 || cols !=3) {
        std::cerr << "Bad Prewitt kernel matrix\n";
        return;
    }
    for(int i=0;i<3;i++) {
        kernel[0 + (i*rows)] = -1.0;
        kernel[1 + (i*rows)] = 0.0;
        kernel[2 + (i*rows)] = 1.0;
    }
}

void prewittY_kernel(const int rows, const int cols, double * const kernel) {
    if(rows != 3 || cols !=3) {
        std::cerr << "Bad Prewitt kernel matrix\n";
        return;
    }
    for(int i=0;i<3;i++) {
        kernel[i + (0*rows)] = 1.0;
        kernel[i + (1*rows)] = 0.0;
        kernel[i + (2*rows)] = -1.0;
    }
}

void apply_prewitt(const int rows, const int cols, pixel *in, pixel *out){
    
    double *intensity =(double *) malloc(sizeof(double) * (rows*cols));
    
    
    //Calculate intensities
    for(int i=0; i<rows; i++){
        for(int j=0; j<cols; j++){
            int offset = i + (j*rows);
            
            
            pixel pix = in[offset];
            intensity[offset]= (pix.red+pix.green+pix.blue)/3;
            
        }
    }
    
    
    /*
     arrays to hold stencils and edge values
     */
    double *wittX =(double *) malloc(sizeof(double)*9);
    double *wittY =(double *) malloc(sizeof(double)*9);
    
    prewittX_kernel(3,3,wittX);
    prewittY_kernel(3,3,wittY);
    
    double *Xedges =(double *) malloc(sizeof(double)*(cols*rows));
    double *Yedges = (double *)malloc(sizeof(double)*(cols*rows));
    
    
    for(int i = 0; i < rows; ++i) {
        for(int j = 0; j < cols; ++j) {
            
            const int out_offset = i + (j*rows);
            
            // ...apply the template centered on the pixel...
            for(int x = i - 1, kx = 0; x <= i + 1; ++x, ++kx) {
                for(int y = j - 1, ky = 0; y <= j + 1; ++y, ++ky) {
                    // ...and skip parts of the template outside of the image
                    if(x >= 0 && x < rows && y >= 0 && y < cols) {
                        // Acculate intensities in the output pixel
                        const int in_offset = x + (y*rows);
                        const int k_offset = kx + (ky*3);
                        Xedges[out_offset] += wittX[k_offset] * intensity[in_offset];
                        Yedges[out_offset] += wittY[k_offset] * intensity[in_offset];
                        
                        
                        
                    }
                }
            }
        }
    }
    
    free(wittY);
    free(wittX);
    free(intensity);
    
    double *out_intensity =(double *) malloc(sizeof(double)*(cols*rows));
    
    for(int i=0; i<rows; i++){
        for(int j=0; j<cols; j++){
            int offsett = i + (j*rows);
            
            //             printf("offsett: %d\n",offsett);
            
            out_intensity[offsett] = sqrt((Xedges[offsett]*Xedges[offsett])+(Yedges[offsett]*Yedges[offsett]));
        }
    }
    
    free(Xedges);
    free(Yedges);
    
    
    for(int i=0; i<rows; i++){
        for(int j=0; j<cols; j++){
            
            int offset = i+ (j*rows);
            
            // double intensity = out_intensity[offset];
            
            
            //          printf("intensity[%d] = %f\n",offset,intensity);
            
            out[offset].red =out_intensity[offset];
            out[offset].green = out_intensity[offset];
            out[offset].blue = out_intensity[offset];
            
        }
    }
    
    free(out_intensity);
    
    return;
}


/*
 * The gaussian kernel provides a stencil for blurring images based on a
 * normal distribution
 * The kernel will provide a template for the contribution of near by
 * pixels to a pixel under consideration.
 */
void gaussian_kernel(const int rows, const int cols, const double stddev, double * const kernel) {
    // values needed for the curve
    const double denom = 2.0 * stddev * stddev;
    const double g_denom = M_PI * denom;
    const double g_denom_recip = (1.0/g_denom);
    // accumulator so that values can be normalized to 1
    double sum = 0.0;
    
    // Build the template
    for(int i = 0; i < rows; ++i) {
        for(int j = 0; j < cols; ++j) {
            const double row_dist = i - (rows/2);
            const double col_dist = j - (cols/2);
            const double dist_sq = (row_dist * row_dist) + (col_dist * col_dist);
            const double value = g_denom_recip * exp((-dist_sq)/denom);
            kernel[i + (j*rows)] = value;
            sum += value;
        }
    }
    
    // Normalize
    const double recip_sum = 1.0 / sum;
    for(int i = 0; i < rows; ++i) {
        for(int j = 0; j < cols; ++j) {
            kernel[i + (j*rows)] *= recip_sum;
        }
    }
}

/*
 * Applies a gaussian blur stencil to an image
 */
void apply_stencil(const int radius, const double stddev, const int rows, const int cols, pixel * const in, pixel * const out) {
    const int dim = radius*2+1;
    double kernel[dim*dim];
    gaussian_kernel(dim, dim, stddev, kernel);
    // For each pixel in the image...
    #pragma omp parallel num_threads(8)
    {
    for(int i = 0; i < rows; ++i) {
        for(int j = 0; j < cols; ++j) {
            const int out_offset = i + (j*rows);
            // ...apply the template centered on the pixel...
            for(int x = i - radius, kx = 0; x <= i + radius; ++x, ++kx) {
                for(int y = j - radius, ky = 0; y <= j + radius; ++y, ++ky) {
                    // ...and skip parts of the template outside of the image
                    if(x >= 0 && x < rows && y >= 0 && y < cols) {
                        // Acculate intensities in the output pixel
                        const int in_offset = x + (y*rows);
                        const int k_offset = kx + (ky*dim);
                        out[out_offset].red   += kernel[k_offset] * in[in_offset].red;
                        out[out_offset].green += kernel[k_offset] * in[in_offset].green;
                        out[out_offset].blue  += kernel[k_offset] * in[in_offset].blue;
                    }
                }
            }
        }
    }
    }
}

int main( int argc, char* argv[] ) {
    
    if(argc != 2) {
        std::cerr << "Usage: " << argv[0] << " imageName\n";
        return 1;
    }
    
    // Read image
    Mat image;
    image = imread(argv[1], CV_LOAD_IMAGE_COLOR);
    if(!image.data ) {
        std::cout <<  "Error opening " << argv[1] << std::endl;
        return -1;
    }
    
    // Get image into C array of doubles for processing
    const int rows = image.rows;
    const int cols = image.cols;
    pixel * imagePixels = (pixel *) malloc(rows * cols * sizeof(pixel));
    for(int i = 0; i < rows; ++i) {
        for(int j = 0; j < cols; ++j) {
            Vec3b p = image.at<Vec3b>(i, j);
            imagePixels[i + (j*rows)] = pixel(p[0]/255.0,p[1]/255.0,p[2]/255.0);
        }
    }
    
    // Create output array
    pixel * outPixels = (pixel *) malloc(rows * cols * sizeof(pixel));
    for(int i = 0; i < rows * cols; ++i) {
        outPixels[i].red = 0.0;
        outPixels[i].green = 0.0;
        outPixels[i].blue = 0.0;
    }
    
    pixel *blurPixels = (pixel *)malloc(rows*cols*sizeof(pixel));
    for(int i=0; i<rows*cols; i++){
        blurPixels[i].red = 0.0;
        blurPixels[i].green = 0.0;
        blurPixels[i].blue = 0.0;
    }
    printf("ROWS: %d, COLS: %d\n",image.rows,image.cols);
    // Do the stencil
    struct timespec start_time;
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC,&start_time);
    apply_stencil(3, 32.0, rows, cols, imagePixels, blurPixels);
    free(imagePixels);
    apply_prewitt(rows,cols,blurPixels,outPixels);
    clock_gettime(CLOCK_MONOTONIC,&end_time);
    long msec = (end_time.tv_sec - start_time.tv_sec)*1000 + (end_time.tv_nsec - start_time.tv_nsec)/1000000;
    printf("Stencil application took %dms\n",msec);
    
    // Create an output image (same size as input)
    Mat dest(rows, cols, CV_8UC3);
    // Copy C array back into image for output
    for(int i = 0; i < rows; ++i) {
        for(int j = 0; j < cols; ++j) {
            // printf("outPixels[%d] = %f\n");
            const size_t offset = i + (j*rows);
            //           printf("outPixels[%d] = %f\n",offset,outPixels[offset].red);
            dest.at<Vec3b>(i, j) = Vec3b(floor(outPixels[offset].red * 255.0),
                                         floor(outPixels[offset].green * 255.0),
                                         floor(outPixels[offset].blue * 255.0));
        }
    }
    
    imwrite("out.jpg", dest);
    
    
    
    free(blurPixels);
    free(outPixels);
    return 0;
}
