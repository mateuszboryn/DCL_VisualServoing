/*!
 * \file
 * \brief
 */

#include <memory>
#include <string>

#include "VisualServoPB_Processor.hpp"
#include "Common/Logger.hpp"

namespace Processors {
namespace VisualServoPB {

using Types::Mrrocpp_Proxy::PBReading;

VisualServoPB_Processor::VisualServoPB_Processor(const std::string & name) :
	Base::Component(name) {
	LOG(LTRACE) << "Hello VisualServoPB_Processor\n";
}

VisualServoPB_Processor::~VisualServoPB_Processor() {
	LOG(LTRACE) << "Good bye VisualServoPB_Processor\n";
}

bool VisualServoPB_Processor::onInit() {
	LOG(LTRACE) << "VisualServoPB_Processor::initialize\n";

	// Register data streams, events and event handlers HERE!
	h_onObjectLocated.setup(this, &VisualServoPB_Processor::onObjectLocated);
	registerHandler("onObjectLocated", &h_onObjectLocated);

	h_onObjectNotFound.setup(this, &VisualServoPB_Processor::onObjectNotFound);
	registerHandler("onObjectNotFound", &h_onObjectNotFound);

	registerStream("in_position", &in_position);
	registerStream("in_timestamp", &in_timestamp);
	registerStream("out_reading", &out_reading);

	readingReady = registerEvent("readingReady");

	return true;
}

bool VisualServoPB_Processor::onFinish() {
	LOG(LTRACE) << "VisualServoPB_Processor::finish\n";

	return true;
}

bool VisualServoPB_Processor::onStep() {
	LOG(LTRACE) << "VisualServoPB_Processor::step\n";
	return true;
}

bool VisualServoPB_Processor::onStop() {
	return true;
}

bool VisualServoPB_Processor::onStart() {
	return true;
}

void VisualServoPB_Processor::onObjectLocated() {
	LOG(LTRACE) << "VisualServoPB_Processor::onObjectLocated()\n";
	PBReading pbr;

	saveTime(pbr);

	pbr.objectVisible = true;
	pbr.objectPosition = in_position.read();

	out_reading.write(pbr);
	readingReady->raise();
}

void VisualServoPB_Processor::onObjectNotFound() {
	LOG(LTRACE) << "VisualServoPB_Processor::onObjectNotFound()\n";
	PBReading pbr;

	saveTime(pbr);

	pbr.objectVisible = false;
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 4; ++j) {
			pbr.objectPosition.elements[i][j] = 0;
		}
	}
	out_reading.write(pbr);

	readingReady->raise();
}

void VisualServoPB_Processor::saveTime(Types::Mrrocpp_Proxy::PBReading& reading) {
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
		reading.processingEndSeconds = ts.tv_sec;
		reading.processingEndNanoseconds = ts.tv_nsec;
	} else {
		LOG(LWARNING) << "VisualServoPB_Processor::saveTime(): clock_gettime() != 0";
		reading.processingEndSeconds = 0;
		reading.processingEndNanoseconds = 0;
	}

	if (!in_timestamp.empty()) {
		ts = in_timestamp.read();
		reading.processingStartSeconds = ts.tv_sec;
		reading.processingStartNanoseconds = ts.tv_nsec;
	} else {
		LOG(LWARNING) << "VisualServoPB_Processor::saveTime(): in_timestamp.empty()";
		reading.processingStartSeconds = 0;
		reading.processingStartNanoseconds = 0;
	}

//	double processingDelay = (reading.processingEndSeconds - reading.processingStartSeconds) + 1e-9 * (reading.processingEndNanoseconds - reading.processingStartNanoseconds);
//	double maxAcceptableDelay = 0.035;
//	if(processingDelay > maxAcceptableDelay){
//		LOG(LWARNING) << "processingDelay ("<<processingDelay<<") > maxAcceptableDelay (" << maxAcceptableDelay << ")";
//	}
}

}//: namespace VisualServoPB
}//: namespace Processors
