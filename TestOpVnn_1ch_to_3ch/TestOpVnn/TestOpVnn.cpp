#include "pch.h"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <map>
#include <filesystem>

// Without next pragmas there are many warnings, how correctly fix them? I don't know...
#pragma warning(push)
#pragma warning( disable : 4251 )
#pragma warning( disable : 4275 )
#include <inference_engine.hpp>
#pragma warning(pop)

#include <opencv2/opencv.hpp>

namespace OV_helper
{

// Puts an 1-channel image to the blob
template <typename T>
void matU8ToBlob(const cv::Mat &orig_image, InferenceEngine::Blob::Ptr &blob)
{
	InferenceEngine::SizeVector blobSize = blob->getTensorDesc().getDims();
	const int width    = int(blobSize[3]);
	const int height   = int(blobSize[2]);
	const int channels = int(blobSize[1]);
	if (channels != 1)
	{
		throw std::runtime_error("OV_helper::matU8ToBlob : channels != 1.");
	}
	T *blob_data = blob->buffer().as<T*>();

	cv::Mat resized_image = orig_image;
	if (   static_cast<int>(width ) != orig_image.size().width
		|| static_cast<int>(height) != orig_image.size().height
		)
	{
		cv::resize(orig_image, resized_image, cv::Size(width, height));
	}

	for (int h = 0; h < height; ++h)
	{
		for (int w = 0; w < width; ++w)
		{
			blob_data[h * width + w] = resized_image.at<uchar>(h, w) / 127.5f - 1.f;
		}
	}
}

// Extracts 3-channel image from the FP32 blob
cv::Mat blobFP32_to_img3ch(const InferenceEngine::Blob::Ptr &blob, float border = 0.5)
{
	InferenceEngine::SizeVector blobSize = blob->getTensorDesc().getDims();

	const int width    = int(blobSize[3]);
	const int height   = int(blobSize[2]);
	const int channels = int(blobSize[1]);
	// blob has 4-channel
	if (channels != 4)
	{
		throw std::runtime_error("OV_helper::blobFP32_to_img3ch() : blob channels != 4.");
	}
	float *blob_data = blob->buffer().as<float*>();

	cv::Mat img(height, width, CV_8UC3);
	// Fill an image by segmantation
	for (int h = 0; h < height; ++h)
	{
		for (int w = 0; w < width; ++w)
		{
			float prob[4];
			for (size_t c = 0; c < channels; ++c)
			{
				prob[c] = blob_data[c * width * height + h * width + w];
			}

			img.at<cv::Vec3b>(h, w)[0] = int(255.f * prob[0]);
			img.at<cv::Vec3b>(h, w)[1] = int(255.f * prob[1]);
			img.at<cv::Vec3b>(h, w)[2] = int(255.f * prob[2]);
		}
	}

	return img;
}

}  // namespace OV_helper

int main(int argc, char *argv[])
{
	try
	{
		namespace fs = std::filesystem;
		// Read threads number
		int cores = 1;
		if (argc > 2 && std::string(argv[1]) == "--cores")
		{
			cores = std::stoi(argv[2]);
		}
		else if (argc > 4 && std::string(argv[3]) == "--cores")
		{
			cores = std::stoi(argv[4]);
		}
		std::cout << "#Cores number is: " << cores << std::endl;
		// Set input folder
		std::string input_folder = "input_folder";
		// Set output folder
		std::string output_folder = "output_folder";
		// collect all files in input_folder
		std::vector<std::string> mas;
		for (auto &p : fs::directory_iterator(input_folder))
		{
			std::string path = p.path().string();
			size_t pos = path.find('\\');
			mas.push_back(path.substr(pos + 1));
		}

		// Read model name
		std::string model_name = "final_combined_alpha0.5_weights";
		if (argc > 2 && std::string(argv[1]) == "--model")
		{
			model_name = argv[2];
		}
		else if (argc > 4 && std::string(argv[3]) == "--model")
		{
			model_name = argv[4];
		}
		std::string model_xml = model_name + ".xml";
		std::string model_bin = model_name + ".bin";
		std::cout << "#Sources: " << std::endl;
		std::cout << "    net   source = " << "[ " + model_xml + ", " + model_bin + " ]" << std::endl;
		std::cout << "    input folder = " << input_folder << std::endl;

		// 1 - Read neural net
		InferenceEngine::CNNNetReader network_reader;
		network_reader.ReadNetwork(model_xml);
		network_reader.ReadWeights(model_bin);

		// 2 - Get net from reader class
		InferenceEngine::CNNNetwork network = network_reader.getNetwork();

		// 3 - Get i/o info
		InferenceEngine::InputsDataMap  input_info = network.getInputsInfo();
		InferenceEngine::OutputsDataMap output_info = network.getOutputsInfo();
		if (input_info.size() != 1)
		{
			throw std::runtime_error("input_info.size() != 1");
		}
		if (output_info.size() != 1)
		{
			throw std::runtime_error("output_info.size() != 1");
		}
		std::string  input_name = input_info.begin()->first;
		std::string output_name = output_info.begin()->first;

		// 4 - Load net
		std::map<std::string, std::string> mp;
		mp["CPU_THREADS_NUM"] = std::to_string(cores);
#ifdef WIN32
		auto extension_ptr = InferenceEngine::make_so_pointer<::InferenceEngine::IExtension>(L"cpu_extension_avx2.dll");
#else
		auto extension_ptr = make_so_pointer<::InferenceEngine::IExtension>("libcpu_extension.so");
#endif
		InferenceEngine::Core core;
		core.AddExtension(extension_ptr, "CPU");
		InferenceEngine::ExecutableNetwork executable_network = core.LoadNetwork(network, "CPU", mp);

		// 5 - Create an inference request
		InferenceEngine::InferRequest infer_request = executable_network.CreateInferRequest();

		long long all_inference = 0;
		long long all_matU8ToBlob = 0;
auto begin_t = std::chrono::high_resolution_clock::now();
		for (int i = 0; i < mas.size(); ++i)
		{
			std::string image_name = input_folder + "/" + mas[i];
			cv::Mat frame = cv::imread(image_name, cv::IMREAD_GRAYSCALE);

			auto input = infer_request.GetBlob(input_name);
auto begin_tim = std::chrono::high_resolution_clock::now();
			OV_helper::matU8ToBlob<float>(frame, input);
auto end_tim = std::chrono::high_resolution_clock::now();
			all_matU8ToBlob += std::chrono::duration_cast<std::chrono::nanoseconds>(end_tim - begin_tim).count();
			infer_request.SetBlob(input_name, input);

			// 6 - Infer request
auto begin_time = std::chrono::high_resolution_clock::now();
			infer_request.Infer();
auto end_time = std::chrono::high_resolution_clock::now();
			all_inference += std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - begin_time).count();

			// 7 - Parse an answer
			auto output = infer_request.GetBlob(output_name);
			auto img = OV_helper::blobFP32_to_img3ch(output, 0.5);

			cv::imwrite(output_folder + "/" + mas[i], img);
		}
auto end_t = std::chrono::high_resolution_clock::now();

		long long full_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_t - begin_t).count();

		std::cout << "#Report: " << std::endl;
		std::cout << "    all images are inferred succeful, result is in the folder : " << output_folder << std::endl;
		std::cout << "    images                         = " <<  mas.size()                                    << " \tones" << std::endl;
		std::cout << "    all time inference   only      = " <<  all_inference   / 1000000.                    << " \tms" << std::endl;
		std::cout << "    avg time inference   only      = " << (all_inference   / 1000000.) * 1. / mas.size() << " \tms" << std::endl;
		std::cout << "    all time matU8ToBlob only      = " <<  all_matU8ToBlob / 1000000.                    << " \tms" << std::endl;
		std::cout << "    avg time matU8ToBlob only      = " << (all_matU8ToBlob / 1000000.) * 1. / mas.size() << " \tms" << std::endl;
		std::cout << "    all time (with imread/imwrite) = " << full_time                                      << " \tms" << std::endl;
		std::cout << "    avg time (with imread/imwrite) = " << full_time * 1. / mas.size()                    << " \tms" << std::endl;
	}
	catch (std::exception &e)
	{
		std::cout << "Exception raised: " << e.what() << std::endl;
	}

	std::cout << "Ok return.\n" << std::endl;
	return 0;
}
