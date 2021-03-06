#include "stdafx.h"
#include "http_request_job.h"
#include "http_request.h"
#include "http_response_headers.h"
#include <iostream>

namespace net
{


	HttpRequestJob::HttpRequestJob(asio::io_service& io_service, 
		HttpRequest* request, Delegate* delegate)
		: resolver_(io_service)
		, socket_(io_service)
		, request_(request)
		, delegate_(delegate)
		, cancel_(false)
		, ref_count_(0)
	{
		
	}


	HttpRequestJob::~HttpRequestJob()
	{
		resolver_.cancel();
		socket_.close();
	}


	void HttpRequestJob::AddRef()
	{
		InterlockedIncrement(&ref_count_);
	}

	void HttpRequestJob::Release()
	{
		if (InterlockedDecrement(&ref_count_) == 0)
		{
			delete this;
		}
	}


	void HttpRequestJob::Start()
	{
		AddRef();
		asio::ip::tcp::resolver::query query(request_->GetUrl().host(), "http");
		resolver_.async_resolve(query,
			std::bind(&HttpRequestJob::HandleResolve, this,
			std::placeholders::_1,
			std::placeholders::_2));
	}


	void HttpRequestJob::Redirect(const URL& url)
	{
		
		socket_.get_io_service().post(
			std::bind(&HttpRequestJob::HandleRedirect, this, url));
	}


	void HttpRequestJob::HandleRedirect(URL url)
	{
		request_->SetUrl(url);
		if (delegate_)
			delegate_->OnRedirectUrl(this, url);
		Start();
	}



	void HttpRequestJob::HandleResolve(const asio::error_code& err, asio::ip::tcp::resolver::iterator endpoint_iterator)
	{
		if (!err)
		{
			// Attempt a connection to each endpoint in the list until we
			// successfully establish a connection.
			if (is_canceled()) {
				Release();
				return;
			}
			asio::async_connect(socket_, endpoint_iterator,
				std::bind(&HttpRequestJob::HandleConnect, this,
				std::placeholders::_1));
		}
		else
		{
			if (delegate_)
				delegate_->OnError(this, err);
			Release();
		}
	}

	void HttpRequestJob::HandleConnect(const asio::error_code& err)
	{
		if (!err)
		{
			if (is_canceled()) {
				Release();
				return;
			}

			request_string_.clear();
			request_->WriteToString(&request_string_);
			
			// The connection was successful. Send the request.
			asio::async_write(socket_, 
				asio::buffer(request_string_.c_str(), request_string_.size()),
				std::bind(&HttpRequestJob::HandleWriteRequest, this,
				std::placeholders::_1));
		}
		else
		{
			if (delegate_)
				delegate_->OnError(this, err);
			Release();
		}
	}

	void HttpRequestJob::HandleWriteRequest(const asio::error_code& err)
	{
		if (!err)
		{
			if (is_canceled()) {
				Release();
				return;
			}
			// Read the response status line. The response_ streambuf will
			// automatically grow to accommodate the entire line. The growth may be
			// limited by passing a maximum size to the streambuf constructor.
			asio::async_read_until(socket_, response_, "\r\n\r\n",
				std::bind(&HttpRequestJob::HandleReadHeaders, this,
				std::placeholders::_1,
				std::placeholders::_2));
		}
		else
		{
			if (delegate_)
				delegate_->OnError(this, err);
			Release();
		}
	}

	void HttpRequestJob::HandleReadHeaders(const asio::error_code& err, std::size_t len)
	{
		if (!err)
		{
			if (is_canceled()) {
				Release();
				return;
			}

			asio::streambuf::const_buffers_type buffer = response_.data();
			response_headers_.reset(new HttpResponseHeaders(std::string(
				asio::buffers_begin(buffer), asio::buffers_begin(buffer) + len
				)));
			response_.consume(len);

			if (delegate_)
				delegate_->OnReceivedHeaders(this, response_headers_);
			
			if (is_canceled()) {
				Release();
				return;
			}

			// Start reading remaining data until EOF.
			asio::async_read(socket_, response_,
				asio::transfer_at_least(1),
				std::bind(&HttpRequestJob::HandleReadContent, this,
				std::placeholders::_1,
				std::placeholders::_2));
		}
		else
		{
			std::cout << "Error: " << err << "\n";
			Release();
		}
	}

	void HttpRequestJob::HandleReadContent(const asio::error_code& err, std::size_t len)
	{
		if (!err)
		{
			if (is_canceled()) {
				Release();
				return;
			}

			asio::streambuf::const_buffers_type buffer = response_.data();
			std::size_t buffer_len = response_.size();
			if (delegate_)
				delegate_->OnReceiveContents(this, &asio::buffers_begin(buffer)[0], buffer_len);
			// Write all of the data that has been read so far.
			response_.consume(buffer_len);

			if (is_canceled()) {
				Release();
				return;
			}

			// Continue reading remaining data until EOF.
			asio::async_read(socket_, response_,
				asio::transfer_at_least(1),
				std::bind(&HttpRequestJob::HandleReadContent, this,
				std::placeholders::_1,
				std::placeholders::_2));
		}
		else if (err != asio::error::eof)
		{
			std::cout << "Error: " << err << "\n";
			Release();
		}
		else
		{
			if (delegate_)
				delegate_->OnReceiveComplete(this);
			Release();
		}
	}

	void HttpRequestJob::Cancel()
	{
		cancel_ = true;
		delegate_ = NULL;
		//resolver_.cancel();
		//socket_.cancel();
	}

	

}