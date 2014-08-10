//#include <queue>
#include <thread>
#include <vector>

#define WIN32_MEAN_AND_LEAN
#include <WinSock2.h>

#include "jpeglib.h"

#include "Common/Logging/Log.h"
#include "VideoCommon/AVIDump.h"

HWND m_emuWnd;
int m_width;
int m_height;
std::thread m_socket_listener;
std::vector<SOCKET> m_clients;
bool m_streaming = false;

static const size_t JPEG_MEM_DST_MGR_BUFFER_SIZE = 8 * (1 << 10);
struct jpeg_destination_mem_mgr
{
	jpeg_destination_mgr mgr;
	std::vector<unsigned char> data;
};

/* setup the buffer but we did that in the main function */
static void mem_init_destination(jpeg_compress_struct* cinfo)
{
	jpeg_destination_mem_mgr* dst = (jpeg_destination_mem_mgr*)cinfo->dest;
	dst->data.resize(JPEG_MEM_DST_MGR_BUFFER_SIZE);
	cinfo->dest->next_output_byte = dst->data.data();
	cinfo->dest->free_in_buffer = dst->data.size();
}

/* what to do when the buffer is full; this should almost never
* happen since we allocated our buffer to be big to start with
*/
static boolean mem_empty_output_buffer(jpeg_compress_struct* cinfo)
{
	jpeg_destination_mem_mgr* dst = (jpeg_destination_mem_mgr*)cinfo->dest;
	size_t oldsize = dst->data.size();
	dst->data.resize(oldsize + JPEG_MEM_DST_MGR_BUFFER_SIZE);
	cinfo->dest->next_output_byte = dst->data.data() + oldsize;
	cinfo->dest->free_in_buffer = JPEG_MEM_DST_MGR_BUFFER_SIZE;
	return true;
}
/* finalize the buffer and do any cleanup stuff */
static void mem_term_destination(jpeg_compress_struct* cinfo)
{
	jpeg_destination_mem_mgr* dst = (jpeg_destination_mem_mgr*)cinfo->dest;
	dst->data.resize(dst->data.size() - cinfo->dest->free_in_buffer);
}

static void jpeg_mem_dest(j_compress_ptr cinfo, jpeg_destination_mem_mgr * dst)
{
	cinfo->dest = (jpeg_destination_mgr*)dst;
	cinfo->dest->init_destination = mem_init_destination;
	cinfo->dest->term_destination = mem_term_destination;
	cinfo->dest->empty_output_buffer = mem_empty_output_buffer;
}

static std::vector<unsigned char> write_JPEG_file(const u8* image_buffer, int image_width, int image_height, int quality)
{
	/* This struct contains the JPEG compression parameters and pointers to
	* working space (which is allocated as needed by the JPEG library).
	* It is possible to have several such structures, representing multiple
	* compression/decompression processes, in existence at once.  We refer
	* to any one struct (and its associated working data) as a "JPEG object".
	*/
	struct jpeg_compress_struct cinfo;
	/* This struct represents a JPEG error handler.  It is declared separately
	* because applications often want to supply a specialized error handler
	* (see the second half of this file for an example).  But here we just
	* take the easy way out and use the standard error handler, which will
	* print a message on stderr and call exit() if compression fails.
	* Note that this struct must live as long as the main JPEG parameter
	* struct, to avoid dangling-pointer problems.
	*/
	struct jpeg_error_mgr jerr;
	/* More stuff */
	JSAMPROW row_pointer[1];  /* pointer to JSAMPLE row[s] */
	int row_stride;       /* physical row width in image buffer */

	/* Step 1: allocate and initialize JPEG compression object */

	/* We have to set up the error handler first, in case the initialization
	* step fails.  (Unlikely, but it could happen if you are out of memory.)
	* This routine fills in the contents of struct jerr, and returns jerr's
	* address which we place into the link field in cinfo.
	*/
	cinfo.err = jpeg_std_error(&jerr);
	/* Now we can initialize the JPEG compression object. */
	jpeg_create_compress(&cinfo);


	/* Step 3: set parameters for compression */

	/* First we supply a description of the input image.
	* Four fields of the cinfo struct must be filled in:
	*/
	cinfo.image_width = image_width;  /* image width and height, in pixels */
	cinfo.image_height = image_height;
	cinfo.input_components = 3;       /* # of color components per pixel */
	cinfo.in_color_space = JCS_RGB;   /* colorspace of input image */
	/* Now use the library's routine to set default compression parameters.
	* (You must set at least cinfo.in_color_space before calling this,
	* since the defaults depend on the source color space.)
	*/
	jpeg_set_defaults(&cinfo);
	/* Now you can set any non-default parameters you wish to.
	* Here we just illustrate the use of quality (quantization table) scaling:
	*/
	jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */);

	/* create our in-memory output buffer to hold the jpeg */
	jpeg_destination_mem_mgr dst_mem;

	/* here is the magic */
	jpeg_mem_dest(&cinfo, &dst_mem);

	/* Step 4: Start compressor */

	/* TRUE ensures that we will write a complete interchange-JPEG file.
	* Pass TRUE unless you are very sure of what you're doing.
	*/
	jpeg_start_compress(&cinfo, TRUE);

	/* Step 5: while (scan lines remain to be written) */
	/*           jpeg_write_scanlines(...); */

	/* Here we use the library's state variable cinfo.next_scanline as the
	* loop counter, so that we don't have to keep track ourselves.
	* To keep things simple, we pass one scanline per call; you can pass
	* more if you wish, though.
	*/
	row_stride = image_width * 3; /* JSAMPLEs per row in image_buffer */

	while (cinfo.next_scanline < cinfo.image_height) {
		/* jpeg_write_scanlines expects an array of pointers to scanlines.
		* Here the array is only one element long, but you could pass
		* more than one scanline at a time if that's more convenient.
		*/
		row_pointer[0] = (JSAMPROW)&image_buffer[cinfo.next_scanline * row_stride];
		(void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	/* Step 6: Finish compression */

	jpeg_finish_compress(&cinfo);

	/* Step 7: release JPEG compression object */

	/* This is an important step since it will release a good deal of memory. */
	jpeg_destroy_compress(&cinfo);

	/* And we're done! */
	return dst_mem.data;
}

static const int BUF_SIZE = 1024;
static void HandleConnection(SOCKET client, sockaddr_in& sockaddr)
{
	char temp_buffer[BUF_SIZE];
	while (true)
	{
		int retval = recv(client, temp_buffer, sizeof(temp_buffer), 0);
		if (retval == 0)
		{
			return; // Connection has been closed
		}
		else if (retval == SOCKET_ERROR)
		{
			return; //FIXME(avistream): handle errors...?
		}
		else
		{
			//keep reading until we have a GET request
			//TODO(avistream): some clients may send a HEAD first?
			if (strnicmp(temp_buffer, "GET", 3) == 0)
				break;
		}
	}

	NOTICE_LOG(VIDEO, "Client connected");
	sprintf_s(temp_buffer, sizeof(temp_buffer), "HTTP/1.1 200 OK\r\nContent-type: multipart/x-mixed-replace;boundary=DolphinVideoStream\r\n");
	send(client, temp_buffer, (int)strlen(temp_buffer), 0);

	m_clients.push_back(client);
}

static void Send(SOCKET client, const u8* data, size_t data_size)
{
	char temp_buffer[BUF_SIZE];
	sprintf_s(temp_buffer, sizeof(temp_buffer), "\r\n--DolphinVideoStream\r\nContent-type: image/jpeg\r\nContent-length: %d\r\n\r\n", data_size);
	int ret = send(client, temp_buffer, (int)strlen(temp_buffer), 0);
	if (ret < 0)
	{
		m_clients.erase(std::remove(m_clients.begin(), m_clients.end(), client), m_clients.end());
		return;
	}

	send(client, reinterpret_cast<const char*>(data), (int)data_size, 0);

	send(client, "\r\n", 2, 0);
}

static void Listen()
{
	SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (srv == INVALID_SOCKET)
		return; //FIXME(avistream): handle errors...

	sockaddr_in srv_data = { 0 };
	srv_data.sin_family = AF_INET;
	srv_data.sin_port = htons(3657); //T9 for "dolp"..hin
	srv_data.sin_addr.S_un.S_addr = INADDR_ANY;

	if (bind(srv, reinterpret_cast<sockaddr*>(&srv_data), sizeof(srv_data)) != 0)
		return; //FIXME(avistream): handle errors...
	if (listen(srv, SOMAXCONN) != 0)
		return; //FIXME(avistream): handle errors...

	while (m_streaming)
	{
		sockaddr_in clientSockAddr;
		int clientSockSize = sizeof(clientSockAddr);

		SOCKET client = accept(srv, reinterpret_cast<sockaddr*>(&clientSockAddr), &clientSockSize);

		if (client != INVALID_SOCKET)
			HandleConnection(client, clientSockAddr);
	}

	closesocket(srv);
}

bool AVIDump::Start(HWND hWnd, int w, int h)
{
	NOTICE_LOG(VIDEO, "Start streaming");
	m_emuWnd = hWnd;

	m_width = w;
	m_height = h;
	m_streaming = true;

	m_socket_listener = std::thread(Listen);

	return true; // TODO: did initialization of stuffs work?
}

void AVIDump::Stop()
{
	NOTICE_LOG(VIDEO, "Stop streaming");
	m_streaming = false;
	if (m_socket_listener.joinable())
		m_socket_listener.join();
	for (auto& client : m_clients)
	{
		//TODO(avistream): be nicer?
		closesocket(client);
	}
}

static void FlipImageData(u8 *data, int w, int h, int pixel_width)
{
	// Flip image upside down. Damn OpenGL.
	for (int y = 0; y < h / 2; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			for (int delta = 0; delta < pixel_width; ++delta)
				std::swap(data[(y * w + x) * pixel_width + delta], data[((h - 1 - y) * w + x) * pixel_width + delta]);
		}
	}
}

void AVIDump::AddFrame(const u8* data, int w, int h)
{
	//static bool shown_error = false;
	//if ((w != m_bitmap.biWidth || h != m_bitmap.biHeight) && !shown_error)
	//{
	//	PanicAlert("You have resized the window while dumping frames.\n"
	//		"Nothing sane can be done to handle this.\n"
	//		"Your video will likely be broken.");
	//	shown_error = true;

	//	m_bitmap.biWidth = w;
	//	m_bitmap.biHeight = h;
	//}

	if (!m_clients.empty())
	{
		FlipImageData(const_cast<u8*>(data), w, h, 3);
		std::vector<unsigned char> jpg = write_JPEG_file(data, w, h, 100);
		for (auto& client : m_clients)
		{
			Send(client, jpg.data(), jpg.size());
		}
	}
}
