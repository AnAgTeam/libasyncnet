#pragma once
#include <curlpp/Form.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace asyncnet {
	/**
	* A multipart/form-data file part whose contents come from an in-memory
	* buffer rather than a file on disk (unlike @ref curlpp::FormParts::File).
	*/
	class FileBufferPart : public curlpp::FormPart {
	public:

		/**
		* initialize a File part
		* @param name The name of the field
		* @param content The content of the field
		* @param filename The filename reported for the part
		*/
		FileBufferPart(
			std::string name,
			std::vector<uint8_t> content,
			std::string filename
		);

		/**
		* initialize a File part
		* @param name The name of the field
		* @param content The content of the field
		* @param filename The filename reported for the part
		* @param content_type MIME type of the content
		*/
		FileBufferPart(
			std::string name,
			std::vector<uint8_t> content,
			std::string filename,
			std::string content_type
		);

		virtual ~FileBufferPart() override = default;

		/**
		* This function will return a copy of the instance.
		*/
		virtual FileBufferPart* clone() const override;

	private:

		void add(
			curl_httppost** first,
			curl_httppost** last
		) override;

		const std::vector<uint8_t> content_;
		const std::string filename_;
		const std::string content_type_;
	};
}
