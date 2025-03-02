#include "pch.h"
#include <iostream>
#include <string>
#include <vector>

// Without next pragmas there are many warnings, how correctly fix them? I don't know...
#pragma warning(push)
#pragma warning(disable:4251)
#pragma warning(disable:4275)
#include <inference_engine.hpp>
#pragma warning(pop)

#include <opencv2/opencv.hpp>

// Overload of << operator for output of std::vector
template<class T>
std::ostream & operator << (std::ostream &out, const std::vector<T> &vec)
{
	out << "{ ";
	for (size_t i = 0; i + 1 < vec.size(); ++i)
	{
		out << vec[i] << ", ";
	}
	if (!vec.empty())
	{
		out << vec.back();
	}
	out << " }";
	return out;
}

// Namespace with printing of info parameters for neural network
namespace io_info
{
	template<class T>
	void i_print_info(const T & i_info)
	{
		std::cout << "    io_info->getPrecision() = " << i_info->getPrecision() << std::endl;
		std::cout << "    io_info->getLayout   () = " << i_info->getLayout() << std::endl;
		std::cout << "    io_info->dims           = " << i_info->getTensorDesc().getDims() << std::endl;
	}
}

namespace OV_helper
{

/* Puts an 1-channel image to the blob
 */
template <typename T>
void matU8ToBlob(const cv::Mat &orig_image, InferenceEngine::Blob::Ptr &blob)
{
	InferenceEngine::SizeVector blobSize = blob->getTensorDesc().getDims();
	const int width    = int(blobSize[3]);
	const int height   = int(blobSize[2]);
	const int channels = int(blobSize[1]);
	if (channels != 1)
	{
		throw std::runtime_error("OV_helper::matU8ToBlob() : channels != 1.");
	}
	T* blob_data = blob->buffer().as<T*>();

	// Creating of resized image for w/h of NN
	cv::Mat resized_image(orig_image);
	if (   static_cast<int>(width)  != orig_image.size().width
		|| static_cast<int>(height) != orig_image.size().height
		)
	{
		cv::resize(orig_image, resized_image, cv::Size(width, height));
	}

	for (int h = 0; h < height; h++)
	{
		for (int w = 0; w < width; w++)
		{
			// set blob by normalized image
			blob_data[h * width + w] = resized_image.at<uchar>(h, w) / 256.f - 0.5f;
		}
	}
}

/* Extracts 1-channel image from the FP32 blob
 */
cv::Mat blobFP32_to_img1ch(const InferenceEngine::Blob::Ptr &blob)
{
	InferenceEngine::SizeVector blobSize = blob->getTensorDesc().getDims();

	const int width    = int(blobSize[3]);
	const int height   = int(blobSize[2]);
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
			img.at<uchar>(h, w) = uchar( blob_data[h * width + w] * 256.f );
		}
	}

	return img;
}

}  // namespace OV_helper

int main()
{
	try
	{
		cv::Mat m;
		// Read model name
		std::string model_name = "zf_unet_224_one_channel";
		std::string model_xml = model_name + ".xml";
		std::string model_bin = model_name + ".bin";
		std::cout << "#Source: " << std::endl;
		std::cout << "    net   source = " << "[ " + model_xml + ", " + model_bin + " ]" << std::endl;
		// Read image name
		std::string image_name = "input.png";
		cv::Mat frame = cv::imread(image_name, cv::IMREAD_GRAYSCALE);
		std::cout << "    image source = " << image_name << std::endl;

		// 1 - Read neural net
		InferenceEngine::Core core;
		InferenceEngine::CNNNetReader network_reader;
		network_reader.ReadNetwork(model_xml);
		network_reader.ReadWeights(model_bin);

		// 2 - Get net from reader class
		auto network = network_reader.getNetwork();

		// 3 - Get i/o info
		InferenceEngine::InputsDataMap  input_info  = network.getInputsInfo();
		InferenceEngine::OutputsDataMap output_info = network.getOutputsInfo();
		if (input_info.size() != 1)
		{
			throw std::runtime_error("input_info.size() != 1");
		}
		if (output_info.size() != 1)
		{
			throw std::runtime_error("output_info.size() != 1");
		}

		std::cout << "#input/output names: " << std::endl;
		std::string  input_name = input_info .begin()->first;
		std::string output_name = output_info.begin()->first;
		std::cout << "    input_name  = " << input_name << std::endl << "    output_name = " << output_name << std::endl;
		// Print input parameters
		std::cout << "#Input  info: " << std::endl;
		io_info::i_print_info(input_info.at(input_name));
		// Print output parameters
		std::cout << "#Output info: " << std::endl;
		io_info::i_print_info(output_info.at(output_name));

		// 4 - Load net to executable
#ifdef WIN32
		auto extension_ptr = InferenceEngine::make_so_pointer<::InferenceEngine::IExtension>(L"cpu_extension_avx2.dll");
#else
		auto extension_ptr = make_so_pointer<::InferenceEngine::IExtension>("libcpu_extension.so");
#endif
		core.AddExtension(extension_ptr,"CPU");
		auto executable_network = core.LoadNetwork(network, "CPU");

		// 5 - Create an inference request
		auto infer_request = executable_network.CreateInferRequest();
		auto input = infer_request.GetBlob(input_name);
		OV_helper::matU8ToBlob<float>(frame, input);
		infer_request.SetBlob(input_name, input);

		// 6 - Infer request
		infer_request.Infer();

		// 7 - Parse an answer
		auto output = infer_request.GetBlob(output_name);
		auto img = OV_helper::blobFP32_to_img1ch(output);
		cv::imwrite("output_image.png", img);

		std::cout << "Output is written to " << "output_image.png" << std::endl;
	}
	catch (std::exception &e)
	{
		std::cout << "Exception raised: " << e.what() << std::endl;
	}

	std::cout << "Ok return.\n" << std::endl;
	return 0;
}
