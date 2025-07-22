#pragma once
#include <string>
#include <vector>
#include <exception>
#include <string_view>
#include <curlpp/Form.hpp>

#pragma warning(push, 0)
#include <boost/json.hpp>
#pragma warning(pop)

namespace asyncnet {

	class UrlParameters {
	public:

		UrlParameters() = default;
		UrlParameters(UrlParameters& other) = default;
		UrlParameters(UrlParameters&& other) = default;

		/**
		 * Constructs from values from 'initilizer_list'
		 * @param initial 'initilizer_list' to set value from
		 */
		UrlParameters(std::initializer_list<std::pair<std::string_view, std::string_view>> initial);

		/**
		 * Constructs with all parameters from 'copy' and appends new from 'initializer_list'
		 * @param copy 'UrlParameters' to copy from
		 * @param expanded 'initilizer_list' to append parameters
		 */
		UrlParameters(const UrlParameters& copy, std::initializer_list<std::pair<std::string_view, std::string_view>> expanded);

		/**
		 * Copies all value and appends new from 'initializer_list'
		 * @param added 'initilizer_list' to append parameters
		 * @return Returns expanded UrlParameters
		 */
		UrlParameters expand_copy(std::initializer_list<std::pair<std::string_view, std::string_view>> added) const;

		/**
		 * Sets all values from 'initilizer_list'
		 * @param initial 'initilizer_list' to set value from
		 * @return Returns *this
		 */
		UrlParameters& operator=(std::initializer_list<std::pair<std::string_view, std::string_view>> initial) &;

		/**
		 * Appends all url parameters to the url
		 * @param url Url to apply the parameters
		 * @return Returns new string with url and parameters
		 */
		std::string apply(std::string_view url) const;

		/**
		 * @return Returns copy of url parameters
		 */
		std::string get() const;

		/**
		 * @return Returns true if empty, otherwise false
		 */
		bool empty() const;

	private:
		void append_items(std::initializer_list<std::pair<std::string_view, std::string_view>> items);

		std::string params_;
	};

	/**
	 * Escape all characters in string as url encoded (like ";a" -> "%3Ba")
	 * @param str The string to url escape
	 * @return Returns new escaped string
	 */
	extern std::string url_escape(std::string_view str);

	/**
	 * Parse string as 'boost::json::object'
	 * @param from The string to parse
	 * @return Returns parsed object
	 * @throws @ref boost::system::system_error If parse failed
	 */
	extern boost::json::object parse_json_object(std::string_view from);

	using MultipartPart = utilspp::clone_ptr<curlpp::FormPart>;
	using MultipartFilePart = curlpp::FormParts::File;
	using MultipartContentPart = curlpp::FormParts::Content;

	using MultipartForms = curlpp::Forms;
};

