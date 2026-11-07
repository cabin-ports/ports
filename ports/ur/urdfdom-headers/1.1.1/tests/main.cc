// Builds a small robot model the way a URDF parser fills ModelInterface,
// links it into a tree, and exercises the pose math, the attribute parsers
// and the sensor, state and world types.
#include <urdf_exception/exception.h>
#include <urdf_model/color.h>
#include <urdf_model/joint.h>
#include <urdf_model/link.h>
#include <urdf_model/model.h>
#include <urdf_model/pose.h>
#include <urdf_model/twist.h>
#include <urdf_model/types.h>
#include <urdf_model/utils.h>
#include <urdf_model_state/model_state.h>
#include <urdf_model_state/types.h>
#include <urdf_sensor/sensor.h>
#include <urdf_sensor/types.h>
#include <urdf_world/types.h>
#include <urdf_world/world.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("check failed: %s\n", what);
        ++failures;
    }
}

bool close_to(double a, double b) { return std::fabs(a - b) < 1e-9; }

urdf::LinkSharedPtr add_link(urdf::ModelInterface& model, const std::string& name) {
    urdf::LinkSharedPtr link = std::make_shared<urdf::Link>();
    link->name = name;
    model.links_[name] = link;
    return link;
}

urdf::JointSharedPtr add_joint(urdf::ModelInterface& model, const std::string& name,
                               const std::string& parent, const std::string& child) {
    urdf::JointSharedPtr joint = std::make_shared<urdf::Joint>();
    joint->name = name;
    joint->parent_link_name = parent;
    joint->child_link_name = child;
    model.joints_[name] = joint;
    return joint;
}

template <typename F>
std::string parse_error(F f) {
    try {
        f();
    } catch (const urdf::ParseError& e) {
        return e.what();
    }
    return "";
}

void check_model() {
    urdf::ModelInterface model;
    model.name_ = "arm";
    urdf::LinkSharedPtr base = add_link(model, "base");
    urdf::LinkSharedPtr upper = add_link(model, "upper");
    urdf::LinkSharedPtr tool = add_link(model, "tool");

    urdf::JointSharedPtr shoulder = add_joint(model, "shoulder", "base", "upper");
    shoulder->type = urdf::Joint::REVOLUTE;
    shoulder->axis = urdf::Vector3(0, 0, 1);
    shoulder->limits = std::make_shared<urdf::JointLimits>();
    shoulder->limits->lower = -1.5;
    shoulder->limits->upper = 1.5;
    urdf::JointSharedPtr wrist = add_joint(model, "wrist", "upper", "tool");
    wrist->type = urdf::Joint::FIXED;

    urdf::BoxSharedPtr box = std::make_shared<urdf::Box>();
    box->dim = urdf::Vector3(0.1, 0.2, 0.3);
    base->visual = std::make_shared<urdf::Visual>();
    base->visual->geometry = box;
    urdf::MaterialSharedPtr blue = std::make_shared<urdf::Material>();
    blue->name = "blue";
    check(blue->color.init("0 0 0.8 1"), "material color");
    model.materials_["blue"] = blue;

    std::map<std::string, std::string> parents;
    model.initTree(parents);
    model.initRoot(parents);

    check(model.getName() == "arm", "model name");
    check(model.getRoot() == base, "root link");
    check(upper->getParent() == base && tool->getParent() == upper, "parent links");
    check(upper->parent_joint == shoulder && base->child_joints.size() == 1 &&
              base->child_links.front() == upper,
          "joint wiring");
    check(parents.size() == 2 && parents["tool"] == "upper", "parent tree");
    check(model.getLink("tool") == tool && !model.getLink("missing"), "getLink");
    check(model.getJoint("shoulder")->limits->upper == 1.5 && !model.getJoint("elbow"), "getJoint");
    check(model.getMaterial("blue")->color.b == 0.8f && !model.getMaterial("red"), "getMaterial");
    std::vector<urdf::LinkSharedPtr> links;
    model.getLinks(links);
    check(links.size() == 3, "getLinks");

    urdf::GeometrySharedPtr geometry = base->visual->geometry;
    check(geometry->type == urdf::Geometry::BOX, "geometry type");
    urdf::BoxSharedPtr as_box = urdf::dynamic_pointer_cast<urdf::Box>(geometry);
    check(as_box && close_to(as_box->dim.z, 0.3), "dynamic_pointer_cast to Box");
    check(!urdf::dynamic_pointer_cast<urdf::Sphere>(geometry), "dynamic_pointer_cast to Sphere");
    check(urdf::Mesh().scale.x == 1 && urdf::Joint().type == urdf::Joint::UNKNOWN, "defaults");

    // A joint to an undefined link, and a forest with two roots.
    add_joint(model, "dangling", "tool", "gripper");
    check(parse_error([&] { model.initTree(parents); }) ==
              "child link [gripper] of joint [dangling] not found",
          "initTree rejects an unknown child");
    model.joints_.erase("dangling");
    add_joint(model, "unnamed", "tool", "");
    check(parse_error([&] { model.initTree(parents); }) ==
              "Joint [unnamed] is missing a parent and/or child link specification.",
          "initTree rejects a joint without a child");
    model.joints_.erase("unnamed");
    add_link(model, "floating");
    check(parse_error([&] { model.initRoot(parents); }) ==
              "Two root links found: [base] and [floating]",
          "initRoot rejects two roots");

    model.clear();
    check(!model.getRoot() && model.getName().empty(), "clear");
    check(parse_error([&] { model.initRoot(std::map<std::string, std::string>()); }) ==
              "No root link found. The robot xml is not a valid tree.",
          "initRoot needs a root");
}

void check_pose() {
    urdf::Vector3 v;
    v.init("1 2.5  -3");
    check(v.x == 1 && v.y == 2.5 && v.z == -3, "Vector3::init skips empty pieces");
    check(parse_error([&] { v.init("1 2"); }) ==
              "Parser found 2 elements but 3 expected while parsing vector [1 2]",
          "Vector3::init needs three values");
    check(!parse_error([&] { v.init("1 x 3"); }).empty(), "Vector3::init rejects non-numbers");

    const double pi = 3.14159265358979323846;
    urdf::Rotation yaw90;
    yaw90.setFromRPY(0, 0, pi / 2);
    urdf::Vector3 turned = yaw90 * urdf::Vector3(1, 0, 0);
    check(close_to(turned.x, 0) && close_to(turned.y, 1) && close_to(turned.z, 0), "rotate a vector");

    urdf::Rotation r;
    r.init("0.1 -0.2 0.3");
    double roll, pitch, yaw;
    r.getRPY(roll, pitch, yaw);
    check(close_to(roll, 0.1) && close_to(pitch, -0.2) && close_to(yaw, 0.3), "RPY round trip");

    urdf::Rotation identity = r * r.GetInverse();
    check(close_to(identity.w, 1) && close_to(identity.x, 0) && close_to(identity.y, 0) && close_to(identity.z, 0),
          "q * q^-1");
    urdf::Rotation half = yaw90 * yaw90;
    half.getRPY(roll, pitch, yaw);
    check(close_to(std::fabs(yaw), pi), "composition adds yaw");

    urdf::Rotation q;
    q.setFromQuaternion(0, 0, 2, 0);
    double qx, qy, qz, qw;
    q.getQuaternion(qx, qy, qz, qw);
    check(qz == 1 && qx == 0 && qy == 0 && qw == 0, "setFromQuaternion normalizes");

    urdf::Pose pose;
    pose.position = urdf::Vector3(1, 2, 3) + urdf::Vector3(1, 1, 1);
    check(pose.position.z == 4 && pose.rotation.w == 1, "Pose");

    urdf::Twist twist;
    twist.linear.init("0.5 0 0");
    check(twist.linear.x == 0.5 && twist.angular.z == 0, "Twist");
}

void check_parsers() {
    std::vector<std::string> pieces;
    urdf::split_string(pieces, "a b  c", " ");
    check(pieces.size() == 4 && pieces[2].empty() && pieces[3] == "c", "split_string");
    check(urdf::strToDouble("0.25") == 0.25, "strToDouble");
    bool threw = false;
    try {
        urdf::strToDouble("0.25m");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "strToDouble rejects trailing text");

    urdf::Color color;
    check(color.init("0.1 0.2 0.3 0.4") && color.g == 0.2f && color.a == 0.4f, "Color::init");
    check(!color.init("1 0 0") && color.a == 1.0f, "Color::init needs four values");
    check(!parse_error([&] { color.init("2 0 0 1"); }).empty(), "Color::init rejects out-of-range");
}

void check_state_sensor_world() {
    urdf::ModelState state;
    state.name = "arm";
    state.time_stamp.set(1.25);
    check(state.time_stamp.sec == 1 && state.time_stamp.nsec == 250000000, "Time::set");
    check(close_to(static_cast<double>(state.time_stamp), 1.25), "Time to double");
    state.time_stamp.set(-0.5);
    check(state.time_stamp.sec == -1 && state.time_stamp.nsec == 500000000, "negative Time");
    urdf::JointStateSharedPtr joint_state = std::make_shared<urdf::JointState>();
    joint_state->joint = "shoulder";
    joint_state->position.push_back(0.5);
    state.joint_states.push_back(joint_state);
    check(state.joint_states.front()->position.front() == 0.5, "JointState");

    urdf::LinkSharedPtr mount = std::make_shared<urdf::Link>();
    mount->name = "mount";
    urdf::Sensor sensor;
    sensor.name = "camera";
    std::shared_ptr<urdf::Camera> camera = std::make_shared<urdf::Camera>();
    camera->type = urdf::VisualSensor::CAMERA;
    camera->width = 640;
    camera->height = 480;
    sensor.sensor = camera;
    sensor.setParent(mount);
    check(sensor.getParent() == mount, "Sensor parent");
    check(urdf::dynamic_pointer_cast<urdf::Camera>(sensor.sensor)->width == 640, "Camera");
    check(urdf::Ray().horizontal_samples == 1, "Ray defaults");
    mount.reset();
    check(!sensor.getParent(), "Sensor holds its parent weakly");

    urdf::World world;
    world.name = "lab";
    urdf::Entity entity;
    entity.model = std::make_shared<urdf::ModelInterface>();
    entity.origin.position.init("0 1 0");
    world.models.push_back(entity);
    check(world.models.front().origin.position.y == 1 && world.models.front().model, "World");
}

}  // namespace

int main() {
    check_model();
    check_pose();
    check_parsers();
    check_state_sensor_world();
    if (failures != 0) {
        std::printf("urdfdom-headers: %d failures\n", failures);
        return 1;
    }
    std::printf("urdfdom-headers: ok\n");
    return 0;
}
