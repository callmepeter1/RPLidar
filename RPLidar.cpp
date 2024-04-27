#include <iostream>
#include <iomanip>
#include <unistd.h>
#include "RPLidar.hpp"

#include "lidar/RPLidar.hpp"

using namespace rp_values;
using namespace data_wrappers;

bool RPLidar::init(const char *serial_path){
    if(port.init_port(serial_path, B115200, 0)){
        stop_scan();
        stop_motor();
        return true;
    }else{
	    return false;
	}
}

void RPLidar::print_status(){
	using namespace std;
	//Get data
	InfoData infoData;
	get_info(&infoData);
	HealthData healthData;
	get_health(&healthData);
	SampleRateData sampleRateData;
	get_samplerate(&sampleRateData);

	cout<<"##############################################"<<endl;
	cout<<left<<setw(3)<<"#"<<left<<setw(27)<<"RPLidar Model:"<<setw(15)<<"A"+to_string(infoData.model>>4)+"M"+to_string(infoData.model&0x0F);
	cout<<right<<"#"<<endl;
	cout<<left<<setw(3)<<"#"<<left<<setw(27)<<"Firmware version: "<<setw(15)<<to_string(infoData.firmware_major)+to_string(infoData.firmware_minor);
	cout<<right<<"#"<<endl;
	cout<<left<<setw(3)<<"#"<<left<<setw(27)<<"Lidar Health: "<<setw(15)<<string(healthData.status==rp_values::LidarStatus::LIDAR_OK?"OK":(healthData.status==rp_values::LidarStatus::LIDAR_WARNING?"WARNING":"ERROR"));
	cout<<right<<"#"<<endl;
	cout<<left<<setw(3)<<"#"<<left<<setw(27)<<("Scan sampling period:")<<setw(15)<<to_string(sampleRateData.scan_sample_rate)+"us";
	cout<<right<<"#"<<endl;
	cout<<left<<setw(3)<<"#"<<left<<setw(27)<<("Express sampling period:")<<setw(15)<<to_string(sampleRateData.express_sample_rate)+"us";
	cout<<right<<"#"<<endl;
	cout<<"##############################################"<<endl;

	//Warnings
	if(healthData.status<0){
		cout<<"Warning/Error code:"<<healthData.error_code<<endl;
	}
	if(healthData.status==rp_values::LidarStatus::LIDAR_WARNING){
		cout<<"WARNING: LiDAR in warning state, may deteriorate"<<endl;
	}
	if(healthData.status==rp_values::LidarStatus::LIDAR_ERROR){
		cout<<"ERROR: LiDAR in critical state"<<endl;
	}
	cout<<"Number of returned samples per update: "<<FullScan::CFG_NB_PACKET_PER_TURN*FullScan::CFG_NB_SAMPLE_PER_PACKET<<endl;
}


/**
 * Sends a packet to the LiDAR
 * @param order that is requested to the LiDAR
 * @param payload of the order, in little endian format
 * @return result of the communication
 */
ComResult RPLidar::send_packet(OrderByte order, const std::vector<uint8_t> &payload) {
	RequestPacket requestPacket(order);
	for(uint8_t data : payload){
		requestPacket.add_payload(data);
	}
	uint8_t timeout=5;
	ComResult result;
	do{
		result=port.send_packet(requestPacket);
		if(result == STATUS_ERROR && timeout>0){
			printf("Error: packet send incomplete, try %d/5\n", 5-(--timeout));
		}
	}while(result==STATUS_ERROR && timeout>0);
	if(timeout==0){
		printf("Error: could not send packet for order 0x%X\n\n", order);
		return STATUS_ERROR;
	}
	return STATUS_OK;
}


/**
 * Fills a health data struct with required information from LiDAR
 * @param health_data
 * @return
 */
ComResult RPLidar::get_health(HealthData *health_data) {
	ComResult status=send_packet(GET_HEALTH);
	uint32_t data_len= port.read_descriptor();
	uint8_t* health_array=port.read_data(data_len);
	*health_data=HealthData(health_array);
	delete[] health_array;
	return status;
}


/**
 * Fills a LiDAR information data struct with required information from LiDAR
 * @param info_data
 * @return
 */
rp_values::ComResult RPLidar::get_info(data_wrappers::InfoData *info_data) {
	ComResult status=send_packet(GET_INFO);
	uint32_t data_len= port.read_descriptor();
	uint8_t* raw_info=port.read_data(data_len);
	*info_data=InfoData(raw_info);
	delete[] raw_info;
	return status;
}


/**
 * Fills a sample rate data struct with required information from LiDAR
 * @param sample_rate for express scans (it is actually the period un us)
 * @return result of the communication
 */
ComResult RPLidar::get_samplerate(SampleRateData *sample_rate) {
	send_packet(GET_SAMPLERATE);
	uint32_t data_len= port.read_descriptor();
	uint8_t* sampe_rate_data=port.read_data(data_len);
	*sample_rate=SampleRateData(sampe_rate_data);
	delete[] sampe_rate_data;
	return STATUS_OK;
}


/**
 * Requests the start of the motor to the control module
 * As we do not have any way of knowing if it worked, we have to spam a bit.
 * @return result of the communication
 */
ComResult RPLidar::start_motor() {
	ComResult res=STATUS_OK;
	for(int i=0;i<NUMBER_PWM_TRIES-1;i++) {
		res=set_pwm(DEFAULT_MOTOR_PWM);
	}
	motor_on=res==STATUS_OK;
	return res;
}


/**
 * Requests a stop of the motor to the control module
 * As we do not have any way of knowing if it worked, we have to spam a bit.
 * @return result of the communication
 */
ComResult RPLidar::stop_motor() {
	if(motor_on) {
		ComResult status = STATUS_OK;
		for (int i = 0; i < NUMBER_PWM_TRIES; i++) {
			status = set_pwm(0) == STATUS_OK ? STATUS_OK : STATUS_ERROR;
		}
		return status;
	}
	else{
		return STATUS_OK;
	}
}


/**
 * Send a PWM motor command for speed control
 * @param pwm between 0 and 1023 (inclusive)
 * @return result of the communication
 */
ComResult RPLidar::set_pwm(uint16_t pwm) {
	if(pwm>1023){
		pwm=1023;
	}
	uint8_t bytes[2]={(uint8_t)pwm, (uint8_t)(pwm>>8)}; //Little endian
	return send_packet(SET_PWM, {bytes[0],bytes[1]});
}


/**
 * Requests the start of an express scan.
 * A response descriptor should then be read for data packets length,
 * then the data should be read
 * @return result of the process
 */
bool RPLidar::start() {
	start_motor();
	sleep(2);
	send_packet(EXPRESS_SCAN, {0,0,0,0,0});
	uint32_t data_size = port.read_descriptor();
	scan_on=data_size==DATA_SIZE_EXPRESS_SCAN;
	return motor_on && scan_on;
}


/**
 * Reads express scan packet into a vector<uint8_t>
 * @param output_data : vector to fill
 * @param size : number of bytes to read
 * @param to_sync : in case of loss of synchronization
 * @return result of the communication
 */
rp_values::ComResult RPLidar::read_scan_data(std::vector<uint8_t> &output_data, uint8_t size, bool to_sync) {
	output_data.clear();
	uint8_t* read_data= nullptr;
	uint8_t n_bytes_to_read=size;
	if(to_sync) {
		read_data = port.read_data(1);
		while (((read_data[0] >> 4) != 0xA)) {
			delete[] read_data;
			read_data = port.read_data(1);
			if (read_data == nullptr) {
				return ComResult::STATUS_ERROR;
			}
		}
		output_data.push_back(read_data[0]);
		delete[] read_data;
		n_bytes_to_read--;
	}
	read_data = port.read_data(n_bytes_to_read);
	if(read_data==nullptr){
		return ComResult::STATUS_ERROR;
	}
	for(uint32_t i=0;i<n_bytes_to_read;i++){
		output_data.push_back(read_data[i]);
	}
	delete[] read_data;
	return STATUS_OK;
}


/**
 * Requests a stop of any scan active
 * @return result of the communication
 */
rp_values::ComResult RPLidar::stop_scan() {
	if(scan_on) {
		auto result = send_packet(STOP);
		usleep(100000);
		port.flush();
		return result;
	}
	else{
		return STATUS_OK;
	}
}

int8_t RPLidar::check_new_turn(float next_angle, FullScan &current_scan) {
	return ((next_angle<5) && (current_scan[current_scan.size()-1].first>355)?1:-1); // Petite marge
}

/**
 * Main Loop for processing express scan data
 */
bool RPLidar::process_express_scans() {
	bool error_handling = true;
	current_scan.clear();                                        //Reinitialize scan
	bool wrong_flag = false;                                    //For resynchronization when wrong flags
	std::vector<uint8_t> read_buffer;                    //input buffer
	for(int i=0;i<11;i++) {
		if (current_scan.measurement_id == 32) {    //If we need to restart a new express packet decoding
			current_scan.measurement_id = 0;
			if (current_scan.next_packet.distances.empty()) {    //If there is no next packet (begginning, we only have one packet)
				read_scan_data(read_buffer, DATA_SIZE_EXPRESS_SCAN, wrong_flag);
				int result = current_scan.next_packet.decode_packet_bytes(read_buffer);
				if (error_handling) {
					//Error handling
					if (result == rp_values::ComResult::STATUS_WRONG_FLAG) {
						printf("WRONG FLAGS, WILL SYNC BACK\n");
						current_scan.next_packet.reset();
						current_scan.measurement_id=32;
						i=0;//Restart full scan
						wrong_flag = true;
						continue; //Couldn't decode an Express packet (wrong flags or checksum)
					} else if (result != rp_values::ComResult::STATUS_OK) {
						printf("WRONG CHECKSUM OR COM ERROR, IGNORE PACKET\n");
						current_scan.next_packet.reset();
						current_scan.measurement_id=32;
						i=0;//Restart full scan
						continue;
					}
				}
			}
			current_scan.current_packet = current_scan.next_packet;    //We finished a packet, we go to the next
			read_scan_data(read_buffer, DATA_SIZE_EXPRESS_SCAN, wrong_flag);
			int result = current_scan.next_packet.decode_packet_bytes(read_buffer);
			if (error_handling) {
				//Error handling
				if (result == rp_values::ComResult::STATUS_WRONG_FLAG) {
					printf("WRONG FLAGS, WILL SYNC BACK\n");
					current_scan.next_packet.reset();
					current_scan.measurement_id=32;
					i=0;//Restart full scan
					wrong_flag = true;
					continue; //Couldn't decode an Express packet (wrong flags or checksum)
				} else if (result != rp_values::ComResult::STATUS_OK) {
					printf("WRONG CHECKSUM OR COM ERROR, IGNORE PACKET\n");
					current_scan.next_packet.reset();
					current_scan.measurement_id=32;
					i=0;//Restart full scan
					continue;
				}
				wrong_flag = false;
			}
		}
		current_scan.compute_measurements();
	}
    std::sort(current_scan.begin(), current_scan.end(), [](std::pair<float, uint16_t>& a, std::pair<float, uint16_t>& b){return a.first<b.first;});
    return true;
}

void RPLidar::print_scan() {
	std::cout<<"SCAN: "<<current_scan.size()<<" values. Content: [angle;dist]";
	std::cout<<std::setprecision(2)<<std::fixed;
	for(auto& m:current_scan){
        std::cout<<"["<<std::setfill(' ')<<std::setw(6)<<m.first<<";"<<std::setw(4)<<m.second<<"] ";
	}
	std::cout<<std::endl;
}

void RPLidar::print_deltas() {
    std::cout<<"Delta_angles: ";
    double delta=0;
    uint16_t a=0;
    for(uint16_t i = 0; i < current_scan.size()-1;i++){
        a++;
        double current=current_scan[i+1].first-current_scan[i].first;
        delta+=current;
        std::cout<<std::setprecision(3)<<"[d_a:"<<current<<"] ";
    }
    std::cout<<std::endl;
    std::cout<<a<<"Last_AVG:"<<delta/a;
    std::cout<<std::endl;
}

void RPLidar::update() {
	process_express_scans();
}



bool RPLidar::stop() {
	auto res_scan=stop_scan();
	auto res_motor=stop_motor();
	return res_scan&&res_motor;
}

bool RPLidar::close() {
	return port.close_port()==0;
}

const std::vector<std::pair<float, uint16_t>>*RPLidar::getDataPoints() const {
	return &current_scan.measurements;
}

RPLidar::~RPLidar() {
	stop();
	close();
}