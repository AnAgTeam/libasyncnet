#include <asyncnet/MultipartForms.hpp>

namespace asyncnet {
	FileBufferPart::FileBufferPart(
		std::string name,
		std::string content,
		std::string filename
	) :
		FormPart(std::move(name)),
		content_(std::move(content)),
		filename_(std::move(filename))
	{

	}

	FileBufferPart::FileBufferPart(
		std::string name,
		std::string content,
		std::string filename,
		std::string content_type
	) :
		FormPart(std::move(name)),
		content_(std::move(content)),
		filename_(std::move(filename)),
		content_type_(std::move(content_type))
	{

	}

	FileBufferPart* FileBufferPart::clone() const {
		return new FileBufferPart(*this);
	}

	void FileBufferPart::add(curl_httppost** first, curl_httppost** last) {
		if (content_type_.empty()) {
			curl_formadd(
				first,
				last,
				CURLFORM_PTRNAME,
				mName.c_str(),
				CURLFORM_BUFFER,
				filename_.c_str(),
				CURLFORM_BUFFERPTR,
				content_.data(),
				CURLFORM_BUFFERLENGTH,
				content_.size(),
				CURLFORM_END
			);
		}
		else {
			curl_formadd(
				first,
				last,
				CURLFORM_PTRNAME,
				mName.c_str(),
				CURLFORM_BUFFER,
				filename_.c_str(),
				CURLFORM_BUFFERPTR,
				content_.data(),
				CURLFORM_BUFFERLENGTH,
				content_.size(),
				CURLFORM_CONTENTTYPE,
				content_type_.c_str(),
				CURLFORM_END
			);
		}
	}
}
