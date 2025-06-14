#include "SoapyPlutoSDR.hpp"
#include <memory>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <algorithm>
#include <chrono>
 #include <unistd.h>
//TODO: Need to be a power of 2 for maximum efficiency ?
# define DEFAULT_RX_BUFFER_SIZE (1 << 16)


std::vector<std::string> SoapyPlutoSDR::getStreamFormats(const int direction, const size_t channel) const
{
	std::vector<std::string> formats;

	formats.push_back(SOAPY_SDR_CS8);
	formats.push_back(SOAPY_SDR_CS12);
	formats.push_back(SOAPY_SDR_CS16);
	formats.push_back(SOAPY_SDR_CF32);

	return formats;
}

std::string SoapyPlutoSDR::getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const
{
	if (direction == SOAPY_SDR_RX) {
		
		fullScale = 2048; // RX expects 12 bit samples LSB aligned
	}
	else if (direction == SOAPY_SDR_TX) {
		fullScale = 32768; // TX expects 12 bit samples MSB aligned
	}
	return SOAPY_SDR_CS16;
}

SoapySDR::ArgInfoList SoapyPlutoSDR::getStreamArgsInfo(const int direction, const size_t channel) const
{
	SoapySDR::ArgInfoList streamArgs;

	return streamArgs;
}


bool SoapyPlutoSDR::IsValidRxStreamHandle(SoapySDR::Stream* handle) const
{
    if (handle == nullptr) {
        return false;
    }

    //handle is an opaque pointer hiding either rx_stream or tx_streamer:
    //check that the handle matches one of them, consistently with direction:
    if (rx_stream) {
        //test if these handles really belong to us:
        if (reinterpret_cast<rx_streamer*>(handle) == rx_stream.get()) {
            return true;
        }
    }

    return false;
}

bool SoapyPlutoSDR::IsValidTxStreamHandle(SoapySDR::Stream* handle) const
{
    if (handle == nullptr) {
        return false;
    }

    //handle is an opaque pointer hiding either rx_stream or tx_streamer:
    //check that the handle matches one of them, consistently with direction:
    if (tx_stream) {
        //test if these handles really belong to us:
        if (reinterpret_cast<tx_streamer*>(handle) == tx_stream.get()) {
            return true;
        }
    }

    return false;
}

SoapySDR::Stream *SoapyPlutoSDR::setupStream(
		const int direction,
		const std::string &format,
		const std::vector<size_t> &channels,
		const SoapySDR::Kwargs &args )
{
	
	//check the format
	plutosdrStreamFormat streamFormat;
	if(!UseExtendedTezukaFeatures)
	{
		if (format == SOAPY_SDR_CF32) {
			SoapySDR_log(SOAPY_SDR_INFO, "Using format CF32.");
			streamFormat = PLUTO_SDR_CF32;
		}
		else if (format == SOAPY_SDR_CS16) {
			SoapySDR_log(SOAPY_SDR_INFO, "Using format CS16.");
			streamFormat = PLUTO_SDR_CS16;
		}
		else if (format == SOAPY_SDR_CS12) {
			SoapySDR_log(SOAPY_SDR_INFO, "Using format CS12.");
			streamFormat = PLUTO_SDR_CS12;
		}
		else if (format == SOAPY_SDR_CS8) {
			
			SoapySDR_log(SOAPY_SDR_INFO, "Using format CS8.");
			streamFormat = PLUTO_SDR_CS8;
			
		}
		

		else {
			throw std::runtime_error(
				"setupStream invalid format '" + format + "' -- Only CS8, CS12, CS16 and CF32 are supported by SoapyPlutoSDR module.");
		}
	}
	else
	{
		if (format == SOAPY_SDR_CF32) {
			SoapySDR_log(SOAPY_SDR_INFO, "Using format CF32 Tezuka.");
			streamFormat = PLUTO_SDR_CF32_TEZUKA;
		}
		else if (format == SOAPY_SDR_CS16) {
			SoapySDR_log(SOAPY_SDR_INFO, "Using format CS16 Tezuka.");
			streamFormat = PLUTO_SDR_CS16_TEZUKA;
		}
		else if (format == SOAPY_SDR_CS12) {
			SoapySDR_log(SOAPY_SDR_INFO, "Using format CS12 Tezuka.");
			streamFormat = PLUTO_SDR_CS12_TEZUKA;
		}
		else if (format == SOAPY_SDR_CS8) {
			
			SoapySDR_log(SOAPY_SDR_INFO, "Using format CS8 Tezuka.");
			streamFormat = PLUTO_SDR_CS8_TEZUKA;
			
		}
	}

	if(direction == SOAPY_SDR_RX){

        std::lock_guard<pluto_spin_mutex> lock(rx_device_mutex);

		iio_channel_attr_write_bool(
			iio_device_find_channel(dev, "altvoltage0", true), "powerdown", false); // Turn ON RX LO

        this->rx_stream = std::unique_ptr<rx_streamer>(new rx_streamer (rx_dev, streamFormat, channels, args));

        return reinterpret_cast<SoapySDR::Stream*>(this->rx_stream.get());
	}

	else if (direction == SOAPY_SDR_TX) {

        std::lock_guard<pluto_spin_mutex> lock(tx_device_mutex);

		iio_channel_attr_write_bool(
			iio_device_find_channel(dev, "altvoltage1", true), "powerdown", false); // Turn ON TX LO

        this->tx_stream = std::unique_ptr<tx_streamer>(new tx_streamer (tx_dev, streamFormat, channels, args));

        return reinterpret_cast<SoapySDR::Stream*>(this->tx_stream.get());
	}

	return nullptr;

}

void SoapyPlutoSDR::closeStream( SoapySDR::Stream *handle)
{
    //scope lock:
    {
        std::lock_guard<pluto_spin_mutex> lock(rx_device_mutex);

        if (IsValidRxStreamHandle(handle)) {
            this->rx_stream.reset();

			iio_channel_attr_write_bool(
				iio_device_find_channel(dev, "altvoltage0", true), "powerdown", true); // Turn OFF RX LO
        }
    }

    //scope lock :
    {
        std::lock_guard<pluto_spin_mutex> lock(tx_device_mutex);

        if (IsValidTxStreamHandle(handle)) {
            this->tx_stream.reset();

			iio_channel_attr_write_bool(
				iio_device_find_channel(dev, "altvoltage1", true), "powerdown", true); // Turn OFF TX LO
        }
    }
}

size_t SoapyPlutoSDR::getStreamMTU( SoapySDR::Stream *handle) const
{
    std::lock_guard<pluto_spin_mutex> lock(rx_device_mutex);

    if (IsValidRxStreamHandle(handle)) {

        return this->rx_stream->get_mtu_size();
    }

	if (IsValidTxStreamHandle(handle)) {
		return 4096;
	}

    return 0;
}

int SoapyPlutoSDR::activateStream(
		SoapySDR::Stream *handle,
		const int flags,
		const long long timeNs,
		const size_t numElems )
{
	if (flags & ~SOAPY_SDR_END_BURST)
		return SOAPY_SDR_NOT_SUPPORTED;

    std::lock_guard<pluto_spin_mutex> lock(rx_device_mutex);

    if (IsValidRxStreamHandle(handle)) {
        return this->rx_stream->start(flags, timeNs, numElems);
    }

	 
    

    return 0;
}

int SoapyPlutoSDR::deactivateStream(
		SoapySDR::Stream *handle,
		const int flags,
		const long long timeNs )
{
    //scope lock:
    {
        std::lock_guard<pluto_spin_mutex> lock(rx_device_mutex);

        if (IsValidRxStreamHandle(handle)) {
            return this->rx_stream->stop(flags, timeNs);
        }
    }

    //scope lock :
    {
        std::lock_guard<pluto_spin_mutex> lock(tx_device_mutex);

        if (IsValidTxStreamHandle(handle)) {
            this->tx_stream->flush();
            return 0;
        }
    }

	return 0;
}

int SoapyPlutoSDR::readStream(
		SoapySDR::Stream *handle,
		void * const *buffs,
		const size_t numElems,
		int &flags,
		long long &timeNs,
		const long timeoutUs )
{
    //the spin_mutex is especially very useful here for minimum overhead !
    std::lock_guard<pluto_spin_mutex> lock(rx_device_mutex);

    if (IsValidRxStreamHandle(handle)) {
        return int(this->rx_stream->recv(buffs, numElems, flags, timeNs, timeoutUs));
    } else {
        return SOAPY_SDR_NOT_SUPPORTED;
    }
}

int SoapyPlutoSDR::writeStream(
		SoapySDR::Stream *handle,
		const void * const *buffs,
		const size_t numElems,
		int &flags,
		const long long timeNs,
		const long timeoutUs )
{
    std::lock_guard<pluto_spin_mutex> lock(tx_device_mutex);

    if (IsValidTxStreamHandle(handle)) {
        return this->tx_stream->send(buffs, numElems, flags, timeNs, timeoutUs);;
    } else {
        return SOAPY_SDR_NOT_SUPPORTED;
    }

}

int SoapyPlutoSDR::readStreamStatus(
		SoapySDR::Stream *stream,
		size_t &chanMask,
		int &flags,
		long long &timeNs,
		const long timeoutUs)
{	
	/*
	 uint32_t val = 0;
				
			iio_device_reg_read(rx_dev, 0x80000088, &val);
            if (val & 4)
            {
                
                iio_device_reg_write(rx_dev, 0x80000088, val);
				//SoapySDR_logf(SOAPY_SDR_INFO, "Rx Overflow !");
				//return SOAPY_SDR_OVERFLOW;
				return SOAPY_SDR_UNDERFLOW;
				//return SOAPY_SDR_CORRUPTION;
				
            }
	*/		
	return 0;
}

void rx_streamer::set_buffer_size_by_samplerate(const size_t samplerate) {

	#define MAX_BUFF_SIZE 32000000LL
    #define MAX_TOTAL_SIZE 60000000LL
    #define MAX_CNT 64

    size_t blockSize = samplerate/8LL; 
    blockSize=(blockSize>>12)<<12;
    if(blockSize>MAX_BUFF_SIZE) blockSize=MAX_BUFF_SIZE;
    size_t  kernel_buffer_cnt=MAX_TOTAL_SIZE/(blockSize);
    if(kernel_buffer_cnt>MAX_CNT) kernel_buffer_cnt=MAX_CNT;
    

	blockSize=blockSize/4;
    this->set_buffer_size(blockSize,kernel_buffer_cnt);

	//this->set_buffer_size(rounded_nb_samples_per_call);
	SoapySDR_logf(SOAPY_SDR_INFO, "Auto setting Buffer Size: %lu with %d kernel ", (unsigned long)blockSize,kernel_buffer_cnt);

    //Recompute MTU from buffer size change.
    //We always set MTU size = Buffer Size.
    //On buffer size adjustment to sample rate,
    //MTU can be changed accordingly safely here.
    set_mtu_size(this->buffer_size);
}

void rx_streamer::set_mtu_size(const size_t mtu_size) {

    this->mtu_size = mtu_size;

    SoapySDR_logf(SOAPY_SDR_INFO, "Set MTU Size: %lu", (unsigned long)mtu_size);
}


rx_streamer::rx_streamer(const iio_device *_dev, const plutosdrStreamFormat _format, const std::vector<size_t> &channels, const SoapySDR::Kwargs &args):
	dev(_dev), buffer_size(DEFAULT_RX_BUFFER_SIZE), buf(nullptr), format(_format), mtu_size(DEFAULT_RX_BUFFER_SIZE)

{
	if (dev == nullptr) {
		SoapySDR_logf(SOAPY_SDR_ERROR, "cf-ad9361-lpc not found!");
		throw std::runtime_error("cf-ad9361-lpc not found!");
	}
	unsigned int nb_channels = iio_device_get_channels_count(dev), i;
	for (i = 0; i < nb_channels; i++)
		iio_channel_disable(iio_device_get_channel(dev, i));

	//default to channel 0, if none were specified
	const std::vector<size_t> &channelIDs = channels.empty() ? std::vector<size_t>{0} : channels;

	for (i = 0; i < channelIDs.size() * 2; i++) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);
		iio_channel_enable(chn);
		channel_list.push_back(chn);
		if((i==1) && (format >= PLUTO_SDR_CF32_TEZUKA))
		{
			fprintf(stderr,"Tezuka CS8 input\n");
			iio_channel_disable(chn);
		}	

	}

	

	if ( args.count( "bufflen" ) != 0 ){

		try
		{
			size_t bufferLength = std::stoi(args.at("bufflen"));
			if (bufferLength > 0)
				this->set_buffer_size(bufferLength,8);
		}
		catch (const std::invalid_argument &){}

	}else{

		long long samplerate;

		iio_channel_attr_read_longlong(iio_device_find_channel(dev, "voltage0", false),"sampling_frequency",&samplerate);

		this->set_buffer_size_by_samplerate(samplerate);

	}
}

rx_streamer::~rx_streamer()
{
	if (buf) {
        iio_buffer_cancel(buf);
        iio_buffer_destroy(buf);
    }

    for (unsigned int i = 0; i < channel_list.size(); ++i) {
        iio_channel_disable(channel_list[i]);
    }



}

size_t rx_streamer::recv(void * const *buffs,
		const size_t numElems,
		int &flags,
		long long &timeNs,
		const long timeoutUs)
{
    //
	if (items_in_buffer <= 0) {

       // auto before = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

	    if (!buf) {
		    return 0;
	    }

		ssize_t ret = iio_buffer_refill(buf);

        // auto after = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

		if (ret < 0)
			return SOAPY_SDR_TIMEOUT;

		items_in_buffer = (unsigned long)ret / iio_buffer_step(buf);

        // SoapySDR_logf(SOAPY_SDR_INFO, "iio_buffer_refill took %d ms to refill %d items", (int)(after - before), items_in_buffer);

		byte_offset = 0;
	}

	size_t items = std::min(items_in_buffer,numElems);

	ptrdiff_t buf_step = iio_buffer_step(buf);

	if (direct_copy) {
		// optimize for single RX, 2 channel (I/Q), same endianess direct copy
		// note that RX is 12 bits LSB aligned, i.e. fullscale 2048
		uint8_t *src = (uint8_t *)iio_buffer_start(buf) + byte_offset;
		int16_t const *src_ptr = (int16_t *)src;
		
		if (format == PLUTO_SDR_CS16) {

			::memcpy(buffs[0], src_ptr, 2 * sizeof(int16_t) * items);

		}
		else if (format == PLUTO_SDR_CF32) {

			float *dst_cf32 = (float *)buffs[0];
			
			for (size_t index = 0; index < items * 2; ++index) {
				*dst_cf32 = float(*src_ptr) / 2048.0f;
				src_ptr++;
				dst_cf32++;
			}

		}
		else if (format == PLUTO_SDR_CS12) {

			int8_t *dst_cs12 = (int8_t *)buffs[0];

			for (size_t index = 0; index < items; ++index) {
				int16_t i = *src_ptr++;
				int16_t q = *src_ptr++;
				// produce 24 bit (iiqIQQ), note the input is LSB aligned, scale=2048
				// note: byte0 = i[7:0]; byte1 = {q[3:0], i[11:8]}; byte2 = q[11:4];
				*dst_cs12++ = uint8_t(i);
				*dst_cs12++ = uint8_t((q << 4) | ((i >> 8) & 0x0f));
				*dst_cs12++ = uint8_t(q >> 4);
			}
		}
		else if (format == PLUTO_SDR_CS8) {

			int8_t *dst_cs8 = (int8_t *)buffs[0];
			
				for (size_t index = 0; index < items * 2; index++) {
					*dst_cs8 = int8_t(*src_ptr >> 4);
					src_ptr++;
					dst_cs8++;
				}
		}
		else if (format == PLUTO_SDR_CS16_TEZUKA) {

			//::memcpy(buffs[0], src_ptr, 2 * sizeof(int16_t) * items);
			int16_t *dst_cs16 = (int16_t *)buffs[0];
			int8_t const *src_ptr_i8 = (int8_t *)src;	
			for (size_t index = 0; index < items * 2; ++index) {
				//*dst_cf32 = float(*src_ptr) / 2048.0f;
				*dst_cs16 = int16_t(*(src_ptr_i8)) << 8 ;

				src_ptr_i8++;
				dst_cs16++;
			}

		}
		else if (format == PLUTO_SDR_CF32_TEZUKA) {

			float *dst_cf32 = (float *)buffs[0];
			int8_t const *src_ptr_i8 = (int8_t *)src;	
			for (size_t index = 0; index < items * 2; ++index) {
				//*dst_cf32 = float(*src_ptr) / 2048.0f;
				*dst_cf32 = float(*(src_ptr_i8)) / 128.0f;

				src_ptr_i8++;
				dst_cf32++;
			}

		}
		else if (format == PLUTO_SDR_CS12_TEZUKA) {

			int8_t *dst_cs12 = (int8_t *)buffs[0];

			for (size_t index = 0; index < items; ++index) {
				int16_t i = *src_ptr++;
				int16_t q = *src_ptr++;
				// produce 24 bit (iiqIQQ), note the input is LSB aligned, scale=2048
				// note: byte0 = i[7:0]; byte1 = {q[3:0], i[11:8]}; byte2 = q[11:4];
				*dst_cs12++ = uint8_t(i);
				*dst_cs12++ = uint8_t((q << 4) | ((i >> 8) & 0x0f));
				*dst_cs12++ = uint8_t(q >> 4);
			}
		}
		else if (format == PLUTO_SDR_CS8_TEZUKA) 
		{
			{
				
				::memcpy(buffs[0], src_ptr, 2* sizeof(int8_t) * items);
			}
		}	
	}
	else {
		int16_t conv = 0, *conv_ptr = &conv;

		for (unsigned int i = 0; i < channel_list.size(); i++) {
			iio_channel *chn = channel_list[i];
			unsigned int index = i / 2;

			uint8_t *src = (uint8_t *)iio_buffer_first(buf, chn) + byte_offset;

			if (format == PLUTO_SDR_CS16) {

				int16_t *dst_cs16 = (int16_t *)buffs[index];

				for (size_t j = 0; j < items; ++j) {
					iio_channel_convert(chn, conv_ptr, src);
					src += buf_step;
					dst_cs16[j * 2 + i] = conv;
				}
			}
			else if (format == PLUTO_SDR_CF32) {

				float *dst_cf32 = (float *)buffs[index];

				for (size_t j = 0; j < items; ++j) {
					iio_channel_convert(chn, conv_ptr, src);
					src += buf_step;
					dst_cf32[j * 2 + i] = float(conv) / 2048.0f;
				}
			}
			else if (format == PLUTO_SDR_CS8) {

				int8_t *dst_cs8 = (int8_t *)buffs[index];
		 
					for (size_t j = 0; j < items; ++j) {
						iio_channel_convert(chn, conv_ptr, src);
						src += buf_step;
						dst_cs8[j * 2 + i] = int8_t(conv >> 4);
					}
			}
			else if (format == PLUTO_SDR_CS8_TEZUKA )
			{
					int8_t *dst_cs8 = (int8_t *)buffs[index];
		 
					for (size_t j = 0; j < items; ++j) {
						iio_channel_convert(chn, conv_ptr, src);
						src += buf_step;
						dst_cs8[j + i] = int8_t(conv);
					}
			}

		}
	}

	items_in_buffer -= items;
	byte_offset += items * iio_buffer_step(buf);

	return(items);

}

int rx_streamer::start(const int flags,
		const long long timeNs,
		const size_t numElems)
{
    //force proper stop before
    stop(flags, timeNs);

    // re-create buffer
	buf = iio_device_create_buffer(dev, buffer_size, false);

	if (!buf) {
		SoapySDR_logf(SOAPY_SDR_ERROR, "Unable to create buffer!");
		throw std::runtime_error("Unable to create buffer!\n");
	}

	direct_copy = has_direct_copy();

	SoapySDR_logf(SOAPY_SDR_INFO, "Has direct RX copy: %d", (int)direct_copy);

	return 0;

}

int rx_streamer::stop(const int flags,
		const long long timeNs)
{
    //cancel first
    if (buf) {
        iio_buffer_cancel(buf);
    }
    //then destroy
	if (buf) {
		iio_buffer_destroy(buf);
		buf = nullptr;
	}

    items_in_buffer = 0;
    byte_offset = 0;

	return 0;

}

void rx_streamer::set_buffer_size(const size_t _buffer_size,const size_t num_kernel){

	if (!buf || this->buffer_size != _buffer_size) {
        //cancel first
        if (buf) {
            iio_buffer_cancel(buf);
        }
        //then destroy
        if (buf) {
            iio_buffer_destroy(buf);
        }

		items_in_buffer = 0;
        byte_offset = 0;


		iio_device_set_kernel_buffers_count(dev, num_kernel);
		buf = iio_device_create_buffer(dev, _buffer_size, false);
		if (!buf) {
			SoapySDR_logf(SOAPY_SDR_ERROR, "Unable to create buffer!");
			throw std::runtime_error("Unable to create buffer!\n");
		}

	}

	this->buffer_size=_buffer_size;
}

size_t rx_streamer::get_mtu_size() {
    return this->mtu_size;
}

// return wether can we optimize for single RX, 2 channel (I/Q), same endianess direct copy
bool rx_streamer::has_direct_copy()
{
	return true; // Fixme
	if (channel_list.size() != 2) // one RX with I + Q
		return false;

	ptrdiff_t buf_step = iio_buffer_step(buf);

	if (buf_step != 2 * sizeof(int16_t))
		return false;

	if (iio_buffer_start(buf) != iio_buffer_first(buf, channel_list[0]))
		return false;

	int16_t test_dst, test_src = 0x1234;
	iio_channel_convert(channel_list[0], &test_dst, (const void *)&test_src);

	return test_src == test_dst;

}


tx_streamer::tx_streamer(const iio_device *_dev, const plutosdrStreamFormat _format, const std::vector<size_t> &channels, const SoapySDR::Kwargs &args) :
	dev(_dev), format(_format), buf(nullptr)
{

	if (dev == nullptr) {
		SoapySDR_logf(SOAPY_SDR_ERROR, "cf-ad9361-dds-core-lpc not found!");
		throw std::runtime_error("cf-ad9361-dds-core-lpc not found!");
	}

	unsigned int nb_channels = iio_device_get_channels_count(dev), i;
	for (i = 0; i < nb_channels; i++)
		iio_channel_disable(iio_device_get_channel(dev, i));

	//default to channel 0, if none were specified
	const std::vector<size_t> &channelIDs = channels.empty() ? std::vector<size_t>{0} : channels;

	for (i = 0; i < channelIDs.size() * 2; i++) {
		iio_channel *chn = iio_device_get_channel(dev, i);
		iio_channel_enable(chn);
		if((i==1) && (format >= PLUTO_SDR_CF32_TEZUKA))
		{
			fprintf(stderr,"Tezuka TX CS8 output\n");
			iio_channel_disable(chn);
		}
		
		
		channel_list.push_back(chn);
	}
	
	if ( args.count( "bufflen" ) != 0 ){

		try
		{
			
			size_t bufferLength = std::stoi(args.at("bufflen"));
			fprintf(stderr,"Tx buflen %d\n",bufferLength);
			if (bufferLength > 0)
				this->set_buffer_size(bufferLength,8);
		}
		catch (const std::invalid_argument &){}

	}else{

		long long samplerate;
		
		iio_channel_attr_read_longlong(iio_device_find_channel(dev, "voltage0", true),"sampling_frequency",&samplerate);
		fprintf(stderr,"Tx SampleRate %d\n",samplerate);
		this->set_buffer_size_by_samplerate(samplerate);

	}


	//buf_size = 4096;
	//items_in_buf = 0;
	/*
	buf = iio_device_create_buffer(dev, buffer_size, false);
	if (!buf) {
		SoapySDR_logf(SOAPY_SDR_ERROR, "Unable to create buffer!");
		throw std::runtime_error("Unable to create buffer!");
	}
	*/
	direct_copy = has_direct_copy();

	SoapySDR_logf(SOAPY_SDR_INFO, "Has direct TX copy: %d", (int)direct_copy);

}

tx_streamer::~tx_streamer(){

	if (buf) { iio_buffer_destroy(buf); }

	for(unsigned int i=0;i<channel_list.size(); ++i)
		iio_channel_disable(channel_list[i]);

}

int tx_streamer::send(	const void * const *buffs,
		const size_t numElems,
		int &flags,
		const long long timeNs,
		const long timeoutUs )

{
    if (!buf) {
		fprintf(stderr,"erro buf\n");
        return 0;
    }
	//fprintf(stderr,"Num elets cs16 tezuka %d\n",numElems);
	size_t items = std::min(buffer_size - items_in_buffer, numElems);
	//size_t items = numElems;
	int16_t src = 0;
	int16_t const *src_ptr = &src;
	
	
	if (format == PLUTO_SDR_CS16) {
			int16_t *dst_ptr = (int16_t *)iio_buffer_start(buf)+items_in_buffer*2 ;
			
			int16_t *samples_cs16 = (int16_t *)buffs[0];

			for (size_t j = 0; j < items; j++)
			{
				 
				dst_ptr[j]=(samples_cs16[j*2]);
				dst_ptr[j+1]=(samples_cs16[j*2+1]);
				//dst_ptr[j]=((samples_cs16[j*2])>>8)|((samples_cs16[j*2])&0xFF)<<8;
				//dst_ptr[j+1]=((samples_cs16[j*2+1])>>8)|((samples_cs16[j*2+1])&0xFF)<<8;	

			}
			
			
		}

	 if (format == PLUTO_SDR_CS16_TEZUKA) {
			//fprintf(stderr,"Num elets cs16 tezuka %d\n",numElems);
			
			int8_t *dst_ptr = (int8_t *)iio_buffer_start(buf)+items_in_buffer*2 ;
			//dst_ptr+=numElems*2;
			int16_t *samples_cs16 = (int16_t *)buffs[0];

			for (size_t j = 0; j < items; j++)
			{
				
				
				//dst_ptr[j]=(samples_cs16[j*2])>>8;
				//dst_ptr[j+1]=(samples_cs16[j*2+1])>>8;

				dst_ptr[j]=(samples_cs16[j*2])>>8;
				dst_ptr[j+1]=(samples_cs16[j*2+1])>>8;

			}
			
			
		}

	items_in_buffer+=items;
	
	if(items_in_buffer==buffer_size)
	{
		//int nbbyte= iio_buffer_push_partial(buf,items_in_buffer);
		//fprintf(stderr,"Push Num numelement %d/%d\n",items_in_buffer,buffer_size);
		
		int nbbyte= iio_buffer_push(buf);
		//fprintf(stderr,"Num writtend %d\n",nbbyte);

		items_in_buffer=0;
		if(items!=numElems) fprintf(stderr,"Buffer is not aligned\n");
	}	
	

	return items;

}

int tx_streamer::flush()
{
	return send_buf();
}

int tx_streamer::send_buf()
{
    if (!buf) {
        return 0;
    }

	if (items_in_buffer > 0) {
		fprintf(stderr,"items_in_buffer %d\n",items_in_buffer);
		if (items_in_buffer < buffer_size) {
			ptrdiff_t buf_step = iio_buffer_step(buf);
			uint8_t *buf_ptr = (uint8_t *)iio_buffer_start(buf) + items_in_buffer * buf_step;
			uint8_t *buf_end = (uint8_t *)iio_buffer_end(buf);

			memset(buf_ptr, 0, buf_end - buf_ptr);
		}

		ssize_t ret = iio_buffer_push(buf);
		items_in_buffer = 0;

		if (ret < 0) {
			return ret;
		}

		return int(ret / iio_buffer_step(buf));
	}

	return 0;

}

// return wether can we optimize for single TX, 2 channel (I/Q), same endianess direct copy
bool tx_streamer::has_direct_copy()
{

	return true;
	/*
	if (channel_list.size() != 2) // one TX with I/Q
		return false;

	ptrdiff_t buf_step = iio_buffer_step(buf);

	if (buf_step != 2 * sizeof(int16_t))
		return false;

	if (iio_buffer_start(buf) != iio_buffer_first(buf, channel_list[0]))
		return false;

	int16_t test_dst, test_src = 0x1234;
	iio_channel_convert_inverse(channel_list[0], &test_dst, (const void *)&test_src);

	return test_src == test_dst;
	*/

}

void tx_streamer::set_buffer_size(const size_t _buffer_size,const size_t num_kernel){

	if (!buf || this->buffer_size != _buffer_size) {
        //cancel first
        if (buf) {
            iio_buffer_cancel(buf);
        }
        //then destroy
        if (buf) {
            iio_buffer_destroy(buf);
        }

		items_in_buffer = 0;
        //byte_offset = 0;


		iio_device_set_kernel_buffers_count(this->dev, num_kernel);
		buf = iio_device_create_buffer(this->dev, _buffer_size, false);
		if (!buf) {
			SoapySDR_logf(SOAPY_SDR_ERROR, "Unable to create buffer!");
			throw std::runtime_error("Unable to create buffer!\n");
		}

	}

	this->buffer_size=_buffer_size;
}

size_t tx_streamer::get_mtu_size() {
    return this->mtu_size;
}


void tx_streamer::set_buffer_size_by_samplerate(const size_t samplerate) {

	#define MAX_BUFF_SIZE 32000000LL
    #define MAX_TOTAL_SIZE 60000000LL
    #define MAX_CNT 64

    //size_t blockSize = samplerate/8LL; 
	size_t blockSize = 1024*1280;
    blockSize=(blockSize>>12)<<12;
    if(blockSize>MAX_BUFF_SIZE) blockSize=MAX_BUFF_SIZE;
    size_t  kernel_buffer_cnt=MAX_TOTAL_SIZE/(blockSize);
    if(kernel_buffer_cnt>MAX_CNT) kernel_buffer_cnt=MAX_CNT;
    

	blockSize=blockSize/4;
    this->set_buffer_size(blockSize,kernel_buffer_cnt);

	//this->set_buffer_size(rounded_nb_samples_per_call);
	SoapySDR_logf(SOAPY_SDR_INFO, "Auto setting Buffer Size: %lu with %d kernel ", (unsigned long)blockSize,kernel_buffer_cnt);

    //Recompute MTU from buffer size change.
    //We always set MTU size = Buffer Size.
    //On buffer size adjustment to sample rate,
    //MTU can be changed accordingly safely here.
    set_mtu_size(this->buffer_size);
}

void tx_streamer::set_mtu_size(const size_t mtu_size) {

    this->mtu_size = mtu_size;

    SoapySDR_logf(SOAPY_SDR_INFO, "Set MTU Size: %lu", (unsigned long)mtu_size);
}