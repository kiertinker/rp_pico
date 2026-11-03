#include "lwip/init.h"
#include "lwip/apps/httpd.h"
#include "pico/mutex.h"

#include "http_server.h"
#include "utils/event_lock.h"

#include <stdio.h>

#include <string>
#include <cstring>

class HttpServer {
 public:
  static u16_t dynamicSsiHandler(int iIndex, char *pcInsert, int iInsertLen, u16_t current_tag_part, u16_t *next_tag_part);
  static const char* cgiHandler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]);
  HttpServer(
      std::string_view page_title,
      const std::vector<std::string_view>& value_descriptions,
      ConfigurationValues* config_values);
  void setLongDynamicData(
      const std::vector<std::string_view>& value_descriptions,
      const std::vector<const char*>& values,
      bool empty_value_is_error);
  ~HttpServer();

 private:
  std::string long_dynamic_data_;
  static const tCGI cgi_handlers[];
  const std::vector<std::string_view>& value_descriptions_;
  std::string_view page_title_;
  ConfigurationValues* config_values_;
  static HttpServer* instance_;
  mutex_t mtx_;
};

HttpServer* HttpServer::instance_ = nullptr;
const tCGI HttpServer::cgi_handlers[] = {{ "/configs.cgi", HttpServer::cgiHandler }};

// Multipart SSI Handler Implementation
u16_t HttpServer::dynamicSsiHandler(int iIndex, char *pcInsert, int iInsertLen, u16_t current_tag_part, u16_t *next_tag_part)
{
  if (instance_ == nullptr) return 0;
  // Since we only have two dynamic SSI tags (one for the page title and the other for the text input fields),
  // we can set the tag substitution based on whether or not iIndex is 0.
  std::string_view data_view(index == 0 ? instance_->page_title_ : instance_->long_dynamic_data_);
  u32_t offset = (u32_t)current_tag_part;
  u32_t data_len = data_view.size();

  if (offset >= data_len) return 0; // Completed execution
  
  // Determine how many bytes we can fit into this chunk
  u32_t remaining = data_len - offset;
  u32_t chunk_size = (remaining < (u32_t)iInsertLen) ? remaining : (u32_t)(iInsertLen - 1);

  // Copy out the chunk
  memcpy(pcInsert, &data_view[offset], chunk_size);
  pcInsert[chunk_size] = '\0'; // Always null-terminate

  // Calculate next offset
  u32_t next_offset = offset + chunk_size;

  // If there are more parts remaining, update next_tag_part so lwIP calls us back,
  // otherwise leave it as is (i.e. 65535 which means HTTPD_LAST_TAG_PART) to indicate completion.
  if (next_offset < data_len) *next_tag_part = (u16_t)next_offset;
  return (u16_t)chunk_size;
}

const char* HttpServer::cgiHandler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
  // If we don't have an active instance of the HttpServer, something went really wrong, so return the error page.
  if (instance_ == nullptr) return "/error.html";

  // Ensure the request is hitting our registered index
  if (iIndex == 0) {
    std::vector<const char*> values(pcValue, pcValue + iNumParams);
    if (!(*instance_->config_values_)(values)) {
      instance_->setLongDynamicData(instance_->value_descriptions_, values, true);
      return "/index.shtml"; // Go back to the index page so invalid values can be corrected.
    }
  } else {
    mutex_exit(&instance_->mtx_);
    return "/error.html"; // Return the error page for unknown index
  }
    
  // Return the URI of the success page the browser should redirect to after executing the CGI
  // and release the mutex to unblock the core "thread" that is waiting for the configuration values to be processed.
  mutex_exit(&instance_->mtx_);
  return "/success.html"; 
}

HttpServer::HttpServer(
    std::string_view page_title, const std::vector<std::string_view>& value_descriptions, ConfigurationValues* config_values)
    : page_title_(page_title), value_descriptions_(value_descriptions), config_values_(config_values) {
  mutex_init(&mtx_);
  instance_ = this;
  setLongDynamicData(value_descriptions, std::vector<const char*>(value_descriptions.size(), ""), false);
  httpd_init();
  static const char* ssi_tag[] = {"fields"};
  http_set_ssi_handler(HttpServer::dynamicSsiHandler, reinterpret_cast<const char**>(ssi_tag), 1);
  http_set_cgi_handlers(cgi_handlers, 1);
}

HttpServer::~HttpServer() {
  // EventLock destructor blocks until the mutex is released by the CGI handler,
  // which indicates that the configuration values have been successfully processed.
  { EventLock lock(mtx_); }
  // Sleep for a few seconds to allow the HTTP server to handle the "success" page redirect before shutting down.
  // This is a best effort workaround for the fact that the HTTP server does not have a built-in mechanism
  // to wait for the client to finish loading the page.
  sleep_ms(5000);
  instance_ = nullptr;
}

void HttpServer::setLongDynamicData(
    const std::vector<std::string_view>& value_descriptions,
    const std::vector<const char*>& values,
    bool empty_value_is_error) {
  long_dynamic_data_.clear();

  // The CGI redirect "get" request requires unique id/names for each text input field,
  // but we don't know how many fields there will be at compile time,
  // so we have to dynamically generate the HTML for the text input fields.
  // We are allowing up to 1000 text input fields, which is more than enough for our use case.
  constexpr size_t TEXT_ID_HUNDREDS = 27;
  constexpr size_t TEXT_ID_TENS = 28;
  constexpr size_t TEXT_ID_ONES = 29;
  constexpr size_t TEXT_NAME_HUNDREDS = 42;
  constexpr size_t TEXT_NAME_TENS = 43;
  constexpr size_t TEXT_NAME_ONES = 44;
  std::string first_part("<input type=\"text\" id=\"text000\" name=\"text000\" placeholder=\"");
  for (int i = 0; i < value_descriptions.size(); ++i) {
    first_part.at(TEXT_ID_HUNDREDS) = '0' + (i / 100) % 10;
    first_part.at(TEXT_ID_TENS) = '0' + (i / 10) % 10;
    first_part.at(TEXT_ID_ONES) = '0' + i % 10;
    first_part.at(TEXT_NAME_HUNDREDS) = '0' + (i / 100) % 10;
    first_part.at(TEXT_NAME_TENS) = '0' + (i / 10) % 10;
    first_part.at(TEXT_NAME_ONES) = '0' + i % 10;
    long_dynamic_data_ += first_part;
    long_dynamic_data_ += value_descriptions[i];
    // empty_value_is_error is set, then we are processing the "fields" SSI tag after a failed CGI submission.
    // In this case, empty values entries indicate errors and we want to highlight any empty fields in red
    // and populate the text input fields with valid values that were submitted (i.e. where they are not nullptr).
    // Otherwise, we are processing the "fields" SSI tag for the first time and we don't want to highlight any fields in red.
    if (empty_value_is_error) {
      if (values[i] == nullptr) {
        long_dynamic_data_ += "\" style=\"border: 2px solid #e74c3c;margin-bottom: 15px;";
      } else {
        long_dynamic_data_ += "\"  style=\"margin-bottom: 15px;\" value=\"";
        long_dynamic_data_ += values[i];
      }
    } else {
      long_dynamic_data_ += "\"  style=\"margin-bottom: 15px;";
    }
    long_dynamic_data_ += "\"><br>";
  }
}


void getConfigsFromHttpServer(
    std::string_view page_title,
    const std::vector<std::string_view>& value_descriptions,
    ConfigurationValues* config_values) {
  HttpServer http_server(page_title, value_descriptions, config_values);
}