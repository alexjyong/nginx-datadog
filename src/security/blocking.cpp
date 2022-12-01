#include "blocking.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string_view>

#include "util.h"

extern "C" {
#include <ngx_http.h>
}

// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast)

using namespace std::literals;

namespace {

namespace dns = datadog::nginx::security;

struct block_resp {
  enum class ct {
    HTML,
    JSON,
    NONE,
  };

  int status;
  ct ct;
  std::string_view location;

  block_resp(int status, enum ct ct, std::string_view location) noexcept
      : status{status}, ct{ct}, location{location} {}

  static block_resp calculate_for(const dns::block_spec &spec,
                                  const ngx_http_request_t &req) noexcept {
    int status;
    enum ct ct;

    status = spec.status;

    switch (spec.ct) {
      case dns::block_spec::ct::AUTO:
        ct = determine_ct(req);
        break;
      case dns::block_spec::ct::HTML:
        ct = ct::HTML;
        break;
      case dns::block_spec::ct::JSON:
        ct = ct::JSON;
        break;
      case dns::block_spec::ct::NONE:
        ct = ct::NONE;
        break;
    }

    return {status, ct, spec.location};
  }

  static ngx_str_t content_type_header(enum block_resp::ct ct) {
    switch (ct) {
      case ct::HTML:
        return ngx_string("text/html;charset=utf-8");
      case ct::JSON:
        return ngx_string("application/json");
      default:
        return ngx_string("");
    }
  }

  struct accept_entry {
    std::string_view type;
    std::string_view subtype;
    double qvalue{};

    enum class specificity {
      none,
      asterisk,  // */*
      partial,   // type/*
      full       // type/subtype
    };

    static bool is_more_specific(specificity a, specificity b) {
      using underlying_t = std::underlying_type_t<specificity>;
      return static_cast<underlying_t>(a) > static_cast<underlying_t>(b);
    }
  };

  struct accept_entry_iter {
    ngx_str_t header;
    std::size_t pos;
    std::size_t pos_end{};

    explicit accept_entry_iter(const ngx_str_t &header, std::size_t pos = 0)
        : header{header}, pos{pos} {
      find_end();
    }

    static accept_entry_iter end() {
      return accept_entry_iter{ngx_str_t{0, nullptr}, 0};
    }

    bool operator!=(const accept_entry_iter &other) const {
      return pos != other.pos || header.data != other.header.data ||
             header.len != other.header.len;
    }

    accept_entry_iter &operator++() noexcept {
      if (pos_end == header.len) {
        *this = end();
        return *this;
      }

      pos = pos_end + 1;
      find_end();
      return *this;
    }

    accept_entry operator*() noexcept {
      accept_entry entry;
      entry.qvalue = 1.0;

      auto sv{part_sv()};
      auto slash_pos = sv.find('/');
      if (slash_pos == std::string_view::npos) {
        return entry;
      }
      entry.type = trim(sv.substr(0, slash_pos));

      sv = sv.substr(slash_pos + 1);
      auto semicolon_pos = sv.find(';');
      if (semicolon_pos == std::string_view::npos) {
        entry.subtype = trim(sv);
        return entry;
      }

      entry.subtype = trim(sv.substr(0, semicolon_pos));
      sv = sv.substr(semicolon_pos + 1);
      auto q_pos = sv.find("q=");
      if (q_pos != std::string_view::npos &&
          (q_pos == 0 || sv.at(q_pos - 1) == ' ')) {
        sv = sv.substr(q_pos + 2);
        char *end;
        entry.qvalue = std::strtod(sv.data(), &end);
        if (end == sv.data() || entry.qvalue == HUGE_VAL || entry.qvalue <= 0 ||
            entry.qvalue > 1) {
          entry.qvalue = 1.0;
        }
      }

      return entry;
    }

   private:
    void find_end() {
      std::string_view const sv{rest_sv()};
      auto colon_pos = sv.find(',');
      if (colon_pos == std::string_view::npos) {
        pos_end = header.len;
      } else {
        pos_end = pos + colon_pos;
      }
    }

    std::string_view rest_sv() const {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      return std::string_view{reinterpret_cast<char *>(header.data + pos),
                              header.len - pos};
    }

    std::string_view part_sv() const {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      return std::string_view{reinterpret_cast<char *>(header.data + pos),
                              pos_end - pos};
    }

    static std::string_view trim(std::string_view sv) {
      while (std::isspace(sv.front())) { 
        sv.remove_prefix(1);
      }
      while (std::isspace(sv.back())) {
        sv.remove_suffix(1);
      }
      return sv;
    }
  };

  static enum ct determine_ct(const ngx_http_request_t &req) {
    if (req.headers_in.accept == nullptr) {
      return ct::JSON;
    }

    accept_entry_iter it{req.headers_in.accept->value};

    using specificity = accept_entry::specificity;
    specificity json_spec{};
    specificity html_spec{};
    double json_qvalue = 0.0;
    size_t json_pos = 0;
    double html_qvalue = 0.0;
    size_t html_pos = 0;

    for (size_t pos = 0; it != accept_entry_iter::end(); ++it, ++pos) {
      accept_entry const ae = *it;

      if (ae.type == "*" && ae.subtype == "*") {
        if (accept_entry::is_more_specific(specificity::asterisk, json_spec)) {
          json_spec = specificity::asterisk;
          json_qvalue = ae.qvalue;
          json_pos = pos;
        }
        if (accept_entry::is_more_specific(specificity::asterisk, html_spec)) {
          html_spec = specificity::asterisk;
          html_qvalue = ae.qvalue;
          html_pos = pos;
        }
      } else if (ae.type == "text" && ae.subtype == "*") {
        if (accept_entry::is_more_specific(specificity::partial, html_spec)) {
          html_spec = specificity::partial;
          html_qvalue = ae.qvalue;
          html_pos = pos;
        }
      } else if (ae.type == "text" && ae.subtype == "html") {
        if (accept_entry::is_more_specific(specificity::full, html_spec)) {
          html_spec = specificity::full;
          html_qvalue = ae.qvalue;
          html_pos = pos;
        }
      } else if (ae.type == "application" && ae.subtype == "*") {
        if (accept_entry::is_more_specific(specificity::partial, json_spec)) {
          json_spec = specificity::partial;
          json_qvalue = ae.qvalue;
          json_pos = pos;
        }
      } else if (ae.type == "application" && ae.subtype == "json") {
        if (accept_entry::is_more_specific(specificity::full, json_spec)) {
          json_spec = specificity::full;
          json_qvalue = ae.qvalue;
          json_pos = pos;
        }
      }
    }

    if (html_qvalue > json_qvalue) {
      return ct::HTML;
    }
    if (json_qvalue > html_qvalue) {
      return ct::JSON;
    }  // equal: what comes first has priority
    if (html_pos < json_pos) {
      return ct::HTML;
    }
    return ct::JSON;
  }
};

const std::string_view default_template_html{
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta "
    "name=\"viewport\" "
    "content=\"width=device-width,initial-scale=1\"><title>You've been "
    "blocked</"
    "title><style>a,body,div,html,span{margin:0;padding:0;border:0;font-size:"
    "100%;font:inherit;vertical-align:baseline}body{background:-webkit-radial-"
    "gradient(26% 19%,circle,#fff,#f4f7f9);background:radial-gradient(circle "
    "at 26% "
    "19%,#fff,#f4f7f9);display:-webkit-box;display:-ms-flexbox;display:flex;-"
    "webkit-box-pack:center;-ms-flex-pack:center;justify-content:center;-"
    "webkit-box-align:center;-ms-flex-align:center;align-items:center;-ms-flex-"
    "line-pack:center;align-content:center;width:100%;min-height:100vh;line-"
    "height:1;flex-direction:column}p{display:block}main{text-align:center;"
    "flex:1;display:-webkit-box;display:-ms-flexbox;display:flex;-webkit-box-"
    "pack:center;-ms-flex-pack:center;justify-content:center;-webkit-box-align:"
    "center;-ms-flex-align:center;align-items:center;-ms-flex-line-pack:center;"
    "align-content:center;flex-direction:column}p{font-size:18px;line-height:"
    "normal;color:#646464;font-family:sans-serif;font-weight:400}a{color:#"
    "4842b7}footer{width:100%;text-align:center}footer "
    "p{font-size:16px}</style></head><body><main><p>Sorry, you cannot access "
    "this page. Please contact the customer service "
    "team.</p></main><footer><p>Security provided by <a "
    "href=\"https://www.datadoghq.com/product/security-platform/"
    "application-security-monitoring/\" "
    "target=\"_blank\">Datadog</a></p></footer></body></html>"sv};

const std::string_view default_template_json{
    "{\"errors\": [{\"title\": \"You've been blocked\", \"detail\": \"Sorry, "
    "you cannot access this page. Please contact the customer service team. "
    "Security provided by Datadog.\"}]}"sv};
}  // namespace

namespace datadog::nginx::security {

// NOLINTNEXTLINE
std::unique_ptr<blocking_service> blocking_service::instance;

void blocking_service::initialize(std::optional<std::string_view> templ_html,
                                  std::optional<std::string_view> templ_json) {
  if (instance) {
    throw std::runtime_error("Blocking service already initialized");
  }
  instance = std::unique_ptr<blocking_service>(
      new blocking_service(templ_html, templ_json));
}

void blocking_service::block(block_spec spec, ngx_http_request_t &req) {
  block_resp const resp = block_resp::calculate_for(spec, req);
  ngx_str_t *templ{};
  if (resp.ct == block_resp::ct::HTML) {
    templ = &templ_html;
  } else if (resp.ct == block_resp::ct::JSON) {
    templ = &templ_json;
  } else {
    req.header_only = 1;
  }

  ngx_http_discard_request_body(&req);

  // TODO: clear all current headers?

  req.headers_out.status = resp.status;
  req.headers_out.content_type = block_resp::content_type_header(resp.ct);
  req.headers_out.content_type_len = req.headers_out.content_type.len;

  if (!resp.location.empty()) {
    push_header(req, "Location"sv, resp.location);
  }
  if (templ) {
    req.headers_out.content_length_n = static_cast<off_t>(templ->len);
  } else {
    req.headers_out.content_length_n = 0;
  }

  // TODO: bypass header filters?
  auto res = ngx_http_send_header(&req);
  if (res == NGX_ERROR || res > NGX_OK || req.header_only) {
    ngx_http_finalize_request(&req, res);
    return;
  }

  ngx_buf_t *b = static_cast<decltype(b)>(ngx_calloc_buf(req.pool));
  if (b == nullptr) {
    ngx_http_finalize_request(&req, NGX_ERROR);
    return;
  }

  b->pos = templ->data;
  b->last = templ->data + templ->len;
  b->last_buf = 1;
  b->memory = 1;

  ngx_chain_t out{};
  out.buf = b;

  // TODO: bypass and call ngx_http_write_filter?
  ngx_http_output_filter(&req, &out);
  ngx_http_finalize_request(&req, NGX_DONE);
}

blocking_service::blocking_service(
    std::optional<std::string_view> templ_html_path,
    std::optional<std::string_view> templ_json_path) {
  if (!templ_html_path) {
    templ_html = ngx_stringv(default_template_html);
  } else {
    custom_templ_html = load_template(*templ_html_path);
    templ_html = ngx_stringv(custom_templ_html);
  }

  if (!templ_json_path) {
    templ_json = ngx_stringv(default_template_json);
  } else {
    custom_templ_json = load_template(*templ_json_path);
    templ_json = ngx_stringv(custom_templ_json);
  }
}

std::string blocking_service::load_template(std::string_view path) {
  std::ifstream const fileStream(std::string{path}, std::ios::binary);
  if (!fileStream) {
    std::string err{"Failed to open file: "};
    err += path;
    throw std::runtime_error(err);
  }

  std::ostringstream s;
  s << fileStream.rdbuf();
  return s.str();
}

void blocking_service::push_header(ngx_http_request_t &req,
                                   std::string_view name, // NOLINT
                                   std::string_view value) {
  ngx_table_elt_t *header =
      static_cast<ngx_table_elt_t *>(ngx_list_push(&req.headers_out.headers));
  if (!header) {
    return;
  }
  header->hash = 1;
  header->key = ngx_stringv(name);
  header->value = ngx_stringv(value);
}

} // namespace datadog::nginx::security 

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast)
