#include <asyncnet/NetTypes.hpp>
#include <curlpp/cURLpp.hpp>

namespace asyncnet {

    UrlParameters::UrlParameters(std::initializer_list<std::pair<std::string_view, std::string_view>> initial) : UrlParameters(UrlParameters(), initial) {}

    UrlParameters::UrlParameters(const UrlParameters& copy, std::initializer_list<std::pair<std::string_view, std::string_view>> expanded) {
        params_ = copy.params_;
        append_items(expanded);
    }

    UrlParameters UrlParameters::expand_copy(std::initializer_list<std::pair<std::string_view, std::string_view>> added) const {
        return UrlParameters(*this, added);
    }

    UrlParameters& UrlParameters::operator=(std::initializer_list<std::pair<std::string_view, std::string_view>> initial)& {
        params_.clear();
        append_items(initial);
        return *this;
    }

    std::string UrlParameters::apply(std::string_view url) const {
        std::string out(url);
        out += "?";
        out += params_;
        return out;
    }

    std::string UrlParameters::get() const {
        return params_;
    }

    bool UrlParameters::empty() const {
        return params_.empty();
    }

    void UrlParameters::append_items(std::initializer_list<std::pair<std::string_view, std::string_view>> items) {
        size_t new_size = params_.size();
        new_size += !params_.empty() ? sizeof('&') : 0;
        for (auto& [key, value] : items) {
            new_size += key.size() + value.size() + 2;
        }
        params_.reserve(new_size);

        if (!params_.empty()) {
            params_ += "&";
        }
        for (auto& [key, value] : items) {
            params_ += key;
            params_ += "=";
            params_ += value;
            params_ += "&";
        }
        if (!params_.empty()) {
            params_.resize(params_.size() - sizeof('&'));
        }
    }

    std::string url_escape(std::string_view str) {
        std::string tmp_str(str);
        return curlpp::escape(tmp_str);
    }

    boost::json::object parse_json_object(std::string_view str) {
        return boost::json::parse(str).as_object();
    }
};
