#include "pch.h"
#include <iostream>
#include <string>

// Without next pragmas there are many warnings, how correctly fix them? I don't know...
#pragma warning(push)
#pragma warning(disable:4251)
#pragma warning(disable:4275)
#include <inference_engine.hpp>
#pragma warning(pop)

#include <opencv2/opencv.hpp>

namespace OV_helper
{

/* Puts an multi-channel image to the blob (tested for 3-channel only).
 */
template <typename T>
void matU8ColorToBlob(const cv::Mat &orig_image, InferenceEngine::Blob::Ptr &blob)
{
	InferenceEngine::SizeVector blobSize = blob->getTensorDesc().getDims();
	const int width    = int(blobSize[3]);
	const int height   = int(blobSize[2]);
	const int channels = int(blobSize[1]);
	T* blob_data = blob->buffer().as<T*>();

	if (channels != orig_image.channels())
	{
		throw std::runtime_error("OV_helper::matU8ColorToBlob : channels != orig_image.channels().");
	}

	cv::Mat resized_image(orig_image);
	if (   static_cast<int>(width) != orig_image.size().width
		|| static_cast<int>(height) != orig_image.size().height
		)
	{
		cv::resize(orig_image, resized_image, cv::Size(width, height));
	}

	for (int c = 0; c < channels; ++c)
	{
		for (int h = 0; h < height; ++h)
		{
			for (int w = 0; w < width; ++w)
			{
				blob_data[c * width * height + h * width + w] = resized_image.at<cv::Vec3b>(h, w)[c] / 256.f - 0.5f;
			}
		}
	}
}

/* Extracts 1-channel image from the FP32 blob
 */
cv::Mat blobFP32_to_img1ch(const InferenceEngine::Blob::Ptr &blob)
{
	InferenceEngine::SizeVector blobSize = blob->getTensorDesc().getDims();

	const int width = int(blobSize[3]);
	const int height = int(blobSize[2]);
	const int channels = int(blobSize[1]);
	if (channels != 1)
	{
		throw std::runtime_error("OV_helper::blobFP32_to_img1ch() : channels != 1.");
	}
	float* blob_data = blob->buffer().as<float*>();

	cv::Mat img(height, width, CV_8U);
	for (int h = 0; h < height; h++)
	{
		for (int w = 0; w < width; w++)
		{
			img.at<uchar>(h, w) = uchar(blob_data[h * width + w] * 256.f);
		}
	}

	return img;
}

} // namespace OV_helper

int main()
{
	try
	{
		// Read the model name
		std::string model_name = "zf_unet_224";
		std::string model_xml = model_name + ".xml";
		std::string model_bin = model_name + ".bin";
		std::cout << "#Sources: " << std::endl;
		std::cout << "    net   source = " << "[ " + model_xml + ", " + model_bin + " ]" << std::endl;
		// Read the image name and the image
		std::string image_name = "input.png";
		cv::Mat frame = cv::imread(image_name, cv::IMREAD_COLOR);
		std::cout << "    image source = " << image_name << std::endl;

		// 1 - Read the network
		InferenceEngine::CNNNetReader network_reader;
		network_reader.ReadNetwork(model_xml);
		network_reader.ReadWeights(model_bin);
		auto network = network_reader.getNetwork();

		// 2 - Get input/output info
		InferenceEngine::InputsDataMap  input_info  = network.getInputsInfo ();
		InferenceEngine::OutputsDataMap output_info = network.getOutputsInfo();
		if (input_info.size() != 1)
		{
			throw std::runtime_error("input_info.size() != 1");
		}
		if (output_info.size() != 1)
		{
			throw std::runtime_error("output_info.size() != 1");
		}
		std::string  input_name = input_info .begin()->first;
		std::string output_name = output_info.begin()->first;

		// 3 - Create executable network
#ifdef WIN32
		auto extension_ptr = InferenceEngine::make_so_pointer<::InferenceEngine::IExtension>(L"cpu_extension_avx2.dll");
#else
		auto extension_ptr = make_so_pointer<::InferenceEngine::IExtension>("libcpu_extension.so");
#endif
		InferenceEngine::Core core;
		core.AddExtension(extension_ptr, "CPU");
		auto executable_network = core.LoadNetwork(network, "CPU");

		// 4 - Create an inference request
		auto infer_request = executable_network.CreateInferRequest();

		// 5 - Set the blob
		auto input = infer_request.GetBlob(input_name);
		OV_helper::matU8ColorToBlob<float>(frame, input);
		infer_request.SetBlob(input_name, input);

		// 6 - Infer request
		infer_request.Infer();

		// 7 - Parse an answer
		auto output = infer_request.GetBlob(output_name);
		auto img = OV_helper::blobFP32_to_img1ch(output);
		cv::imwrite("output_image.png", img);

		std::cout << "The result of inference succesfully saved to " << "output_image.png" << std::endl;
	}
	catch (std::exception &e)
	{
		std::cout << "Exception raised: " << e.what() << std::endl;
	}

	std::cout << "Ok return.\n" << std::endl;
	return 0;
}
