#include "sys.h"
#include "RenderGraph.h"
#include "RenderPass.h"
#include "Attachment.h"
#include "LogicalDevice.h"
#include "SynchronousWindow.h"
#include "debug.h"
#ifdef CWDEBUG
#include "debug_ostream_operators.h"
#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"
#include <boost/graph/graphviz.hpp>
#endif

namespace vulkan::rendergraph {

void RenderGraph::for_each_render_pass(Direction direction, std::function<bool(RenderPass*, std::vector<RenderPass*>&)> lambda) const
{
  DoutEntering(dc::rpverbose, "RenderGraph::for_each_render_pass(" << direction << ", lambda)");
  // As every render pass is reachable from one or more of the sources,
  // we can run over all sources, and from there follow the links to subsequent render passes.
  ++m_traversal_id;
  RenderPass::SearchType search_type = direction == search_forwards ? m_have_incoming_outgoing ? RenderPass::Outgoing : RenderPass::Subsequent : RenderPass::Incoming;
  std::vector<RenderPass*> path; // All in between nodes that led to the callback.
  for (RenderPass* source : m_sources)
  {
    source->for_all_render_passes_until(m_traversal_id, lambda, search_type, path);
    // Paranoia: everything should be popped.
    ASSERT(path.empty());
  }
}

void RenderGraph::for_each_render_pass_from(RenderPass* start, Direction direction, std::function<bool(RenderPass*, std::vector<RenderPass*>&)> lambda) const
{
  DoutEntering(dc::rpverbose, "RenderGraph::for_each_render_pass_from(" << start << ", " << direction << ", lambda)");
  ++m_traversal_id;
  RenderPass::SearchType search_type = direction == search_forwards ? m_have_incoming_outgoing ? RenderPass::Outgoing : RenderPass::Subsequent : RenderPass::Incoming;
  std::vector<RenderPass*> path; // All in between nodes that led to the callback.
  start->for_all_render_passes_until(m_traversal_id, lambda, search_type, path, true);
}

#ifdef CWDEBUG
namespace gv {

using namespace boost;

enum Direction { forwards, backwards };

struct VertexProperties
{
  std::string name;
};

struct EdgeProperties
{
  Direction direction;
};

using Digraph = adjacency_list<vecS, vecS, directedS, VertexProperties, EdgeProperties>;

class EdgeColorWriter
{
 private:
  Digraph& m_graph;

 public:
  // Constructor - needs reference to graph we are coloring.
  EdgeColorWriter(Digraph& g) : m_graph(g) { }

  // Functor that does the coloring.
  template<class Edge>
  void operator()(std::ostream& out, Edge const& edge) const
  {
    // Check if this edge is a forward (false) or backward (true) edge.
    auto direction_map = get(&EdgeProperties::direction, m_graph);
    Direction direction = get(direction_map, edge);
    if (direction == backwards)
      out << "[color=blue]";
  }
};

} // namespace gv
#endif

void RenderGraph::generate(task::SynchronousWindow* owning_window)
{
  DoutEntering(dc::renderpass, "RenderGraph::generate()");

  // Only call generate() once.
  ASSERT(!m_have_incoming_outgoing);
  // Fix m_sources and m_sinks.
  std::vector<RenderPass*> sources;
  std::vector<RenderPass*> sinks;
  for_each_render_pass(search_forwards,
      [&](RenderPass* render_pass, std::vector<RenderPass*>& UNUSED_ARG(path))
      {
        if (!render_pass->has_incoming_vertices())
          sources.push_back(render_pass);
        if (!render_pass->has_outgoing_vertices())
          sinks.push_back(render_pass);
        return false;
      });
  m_sources.swap(sources);
  m_sinks.swap(sinks);
  m_have_incoming_outgoing = true;

  // Make a list of all attachments.
  std::set<Attachment const*, Attachment::CompareIDLessThan> all_attachments;
  for_each_render_pass(search_forwards,
      [&](RenderPass* render_pass, std::vector<RenderPass*>& UNUSED_ARG(path))
      {
        render_pass->add_attachments_to(all_attachments);
        return false;
      });

#ifdef CWDEBUG
  Dout(dc::renderpass|continued_cf, "All attachments: ");
  char const* prefix = "";
  for (Attachment const* attachment : all_attachments)
  {
    Dout(dc::continued, prefix << attachment);
    prefix = ", ";
  }
  Dout(dc::finish, ".");
#endif

  // Run over each attachment.
  for (Attachment const* attachment : all_attachments)
  {
    Dout(dc::renderpass, "Processing attachment \"" << attachment << "\".");
#ifdef CWDEBUG
    NAMESPACE_DEBUG::Mark mark;
#endif

    // Find all render passes that knows about, LOADs and/or STORE this attachment.
    std::vector<RenderPass*> knows;
    std::vector<RenderPass*> loads;
    std::vector<RenderPass*> stores;
    for_each_render_pass(search_forwards,
        [&](RenderPass* render_pass, std::vector<RenderPass*>& UNUSED_ARG(path))
        {
          if (render_pass->is_known(attachment))
            knows.push_back(render_pass);
          if (render_pass->is_load(attachment))
          {
            Dout(dc::renderpass, "Render pass \"" << render_pass << "\" loads attachment \"" << attachment << "\".");
            loads.push_back(render_pass);
          }
          if (render_pass->is_store(attachment))
          {
            Dout(dc::renderpass, "Render pass \"" << render_pass << "\" stores attachment \"" << attachment << "\".");
            stores.push_back(render_pass);
          }
          return false;
        });

    // Run over all the render passes that load this attachment.
    for (RenderPass* render_pass : loads)
    {
      DoutEntering(dc::renderpass, "Finding render pass that stores to \"" << attachment << "\" which is loaded by \"" << render_pass << "\".");
      // Search backwards till a render pass that stores to the attachment.
      // Encountering a clear (that is not storing) is an error.
      // Encountering more than one render pass that stores to the attachment is an error.
      // Finding no render pass that stores to the attachment is an error.
      std::vector<RenderPass*> stores;
      for_each_render_pass_from(render_pass, search_backwards,
          [&](RenderPass* preceding_render_pass, std::vector<RenderPass*>& path)
          {
            DoutEntering(dc::rpverbose|continued_cf, "lambda(" << preceding_render_pass << ", " << path << ") = ");
            if (preceding_render_pass->is_store(attachment))
            {
              stores.push_back(preceding_render_pass);

              // Assuming we won't throw; already tell all render passes along the path that know about attachment that they have to preserve it.
              for (RenderPass* pass : path)
                if (pass->is_known(attachment))
                  pass->get_node(attachment).set_preserve();

              Dout(dc::finish, "true (stop)");
              return true;        // Stop searching.
            }
            if (preceding_render_pass->is_clear(attachment))
              THROW_ALERT("The CLEAR of attachment \"[ATTACHMENT]\" by render pass \"[PRECEDING]\" hides any preceding store needed by render pass "
                  "\"[RENDERPASS]\". Did you mean \"[PRECEDING]\" to store \"[ATTACHMENT]\"?",
                  AIArgs("[ATTACHMENT]", attachment)("[PRECEDING]", preceding_render_pass)("[RENDERPASS]", render_pass));
            Dout(dc::finish, "false (continue)");
            return false;
          });
      if (stores.size() > 1)
        THROW_ALERT("The load of attachment \"[ATTACHMENT]\" by render pass \"[RENDERPASS]\" is ambiguous: "
            "both \"[PASS0]\" and \"[PASS1]\" stores are visible.",
                AIArgs("[ATTACHMENT]", attachment)("[RENDERPASS]", render_pass)("[PASS0]", stores[0])("[PASS1]", stores[1]));
      if (stores.empty())
        THROW_ALERT("The load of attachment \"[ATTACHMENT]\" by render pass \"[RENDERPASS]\" has no visible stores.",
            AIArgs("[ATTACHMENT]", attachment)("[RENDERPASS]", render_pass));
      Dout(dc::renderpass, "The load of \"" << attachment << "\" by \"" << render_pass << "\" was stored by \"" << stores[0] << "\".");
    }

    // Run over all render passes that store this attachment.
    for (RenderPass* render_pass : stores)
    {
      DoutEntering(dc::renderpass, "Checking if (" << render_pass << "/" << attachment << ") is a sink.");
      // Run over all render passes that succeed this render pass and see if there are any that load or clear this attachment.
      // If there are none, then the render pass is a sink for this attachment (it stores to the attachment and then nothing
      // else uses it).
      bool is_sink = true;
      for_each_render_pass_from(render_pass, search_forwards,
          [&](RenderPass* succeeding_render_pass, std::vector<RenderPass*>& CWDEBUG_ONLY(path))
          {
            DoutEntering(dc::rpverbose|continued_cf, "lambda(" << succeeding_render_pass << ", " << path << ") = ");
            if (succeeding_render_pass->is_known(attachment))
            {
              Dout(dc::renderpass, "Not a sink because " << succeeding_render_pass << " which succeeds " << render_pass << " knows about " << attachment << ".");
              is_sink = false;
              Dout(dc::finish, "true (stop)");
              return true;        // Stop searching.
            }
            Dout(dc::finish, "false (continue)");
            return false;
          });
      if (is_sink)
        render_pass->get_node(attachment).set_is_sink();        // Safe to call get_node, because we already know that render_pass knows about attachment (it stores to it).
    }

    // Run over all render passes that know this attachment.
    for (RenderPass* render_pass : knows)
    {
      DoutEntering(dc::renderpass, "Checking if (" << render_pass << "/" << attachment << ") is a source.");
      // Mark attachment as a source unless it is preceded by another render pass that knows about it.
      bool is_source = true;
      for_each_render_pass_from(render_pass, search_backwards,
          [&](RenderPass* preceding_render_pass, std::vector<RenderPass*>& CWDEBUG_ONLY(path))
          {
            DoutEntering(dc::rpverbose|continued_cf, "lambda(" << preceding_render_pass << ", " << path << ") = ");
            if (preceding_render_pass->is_known(attachment))
            {
              Dout(dc::renderpass, "Not a source because " << preceding_render_pass << " which preceeds " << render_pass << " knows about " << attachment << ".");
              is_source = false;
              Dout(dc::finish, "true (stop)");
              return true;        // Stop searching.
            }
            Dout(dc::finish, "false (continue)");
            return false;
          });
      if (is_source)
        render_pass->get_node(attachment).set_is_source();      // Safe to call get_node, because we already know that render_pass knows about attachment.
    }
  }

  // The test suite only generates the graph.
  if (!owning_window)
    return;

#ifdef CWDEBUG
  size_t number_of_registered_attachments = owning_window->number_of_registered_attachments();
  // It should be impossible that this fails (paranoia check). The -1 case holds if we have a swapchain image, which isn't registered.
  ASSERT(number_of_registered_attachments == all_attachments.size() - 1 || number_of_registered_attachments == all_attachments.size());

  for (auto iter = owning_window->attachments_begin(); iter != owning_window->attachments_end(); ++iter)
  {
    Attachment const* attachment = *iter;
    Dout(dc::notice, "Attachment \"" << attachment->name() << "\" with index " << attachment->index() << ".");
  }
#endif

  // Before we can create the render passes, we have to mark any attachment that is used for presentation.
  // Currently we only support one such attachment: the Swapchain::m_presentation_attachment
  // of the owning window.
  auto presentation_attachment_index = owning_window->swapchain().presentation_attachment().index();
  // The swapchain attachment is expected to have an undefined index (paranoia check).
  ASSERT(presentation_attachment_index.undefined());
  // Run over all render passes and mark the attachment with the same id as "presentation" when it is a sink.
  for_each_render_pass(search_forwards,
      [presentation_attachment_index](RenderPass* render_pass, std::vector<RenderPass*>& UNUSED_ARG(path))
      {
        render_pass->set_is_present_on_attachment_sink_with_index(presentation_attachment_index);
        return false;
      });

  // Now we can use get_final_attachment.

  // Run again over each attachment.
  for (Attachment const* attachment : all_attachments)
  {
    // Search backwards from all sink render passes and make sure that there is at most one sink for this attachment.
    DoutEntering(dc::renderpass, "Search for sink of attachment \"" << attachment << "\".");
    RenderPass* sink = nullptr;
    for_each_render_pass(search_backwards,
        [&](RenderPass* preceding_render_pass, std::vector<RenderPass*>& CWDEBUG_ONLY(path))
        {
          DoutEntering(dc::rpverbose|continued_cf, "lambda(" << preceding_render_pass << ", " << path << ") = ");
          if (preceding_render_pass->is_known(attachment))
          {
            auto node = preceding_render_pass->get_node(attachment);
            if (node.is_sink())
            {
              if (sink)
                THROW_ALERT("Attachment \"[ATTACHMENT]\" has more than one render pass (\"[PASS1]\", \"[PASS2]\" ...) marked as sink.",
                    AIArgs("[ATTACHMENT]", attachment)("[PASS1]", sink)("[PASS2]", preceding_render_pass));
              sink = preceding_render_pass;
              Dout(dc::finish, "true (stop)");
              return true;        // Stop searching.
            }
          }
          Dout(dc::finish, "false (continue)");
          return false;
        });
    if (sink)
    {
      Dout(dc::renderpass, "Render pass \"" << sink << "\" is the sink of attachment \"" << attachment << "\".");
      attachment->set_final_layout(sink->get_final_layout(attachment, owning_window->logical_device()->supports_separate_depth_stencil_layouts()));
    }
  }

  // Find the swapchain attachment and the render pass that stores to it.
  Swapchain& swapchain = owning_window->swapchain();
  Attachment const& presentation_attachment = swapchain.presentation_attachment();
  auto iter = std::find_if(all_attachments.begin(), all_attachments.end(),
      [index = presentation_attachment.render_graph_attachment_index()](Attachment const* candidate){ return candidate->render_graph_attachment_index() == index; });
  // Does this render graph know about the swapchain attachment?
  if (iter != all_attachments.end())
  {
    Attachment const* attachment = *iter;
    // Look for a render pass where this attachment is marked as sink.
    RenderPass* sink = nullptr;
    for_each_render_pass(search_forwards,
        [&](RenderPass* render_pass, std::vector<RenderPass*>& CWDEBUG_ONLY(path))
        {
          DoutEntering(dc::rpverbose|continued_cf, "lambda(" << render_pass << ", " << path << ") = ");
          if (render_pass->is_known(attachment))
          {
            auto node = render_pass->get_node(attachment);
            if (node.is_sink())
            {
              sink = render_pass;
              Dout(dc::finish, "true (stop)");
              return true;        // Stop searching.
            }
          }
          Dout(dc::finish, "false (continue)");
          return false;
        });
    if (!sink)
      THROW_ALERT("The swapchain attachment is used in this render graph, but none of the render passes uses it as an output sink.");
    swapchain.set_render_pass_output_sink(static_cast<vulkan::RenderPass*>(sink));
  }

#if 0 //def CWDEBUG
  // Print out the result.
  std::map<RenderPass const*, int> ids;
  std::vector<std::string> names;
  int next_id = 0;
  std::vector<std::pair<int, int>> forwards_edges;
  std::vector<std::pair<int, int>> backwards_edges;
  for_each_render_pass(search_forwards,
      [&](RenderPass* render_pass, std::vector<RenderPass*>& UNUSED_ARG(path)){
        render_pass->print_vertices(ids, names, next_id, forwards_edges, backwards_edges);
        return false;
      });

  // Create a boost graph with ids.size() vertices.
  gv::Digraph g(ids.size());

  // Fill in the names of the vertices.
  for (int v = 0; v < ids.size(); ++v)
    g[v].name = names[v];

  // Add the edges plus meta data.
  ASSERT(forwards_edges.size() == backwards_edges.size());
  for (int e = 0; e < forwards_edges.size(); ++e)
  {
    add_edge(forwards_edges[e].first, forwards_edges[e].second, { gv::forwards }, g);
    add_edge(backwards_edges[e].first, backwards_edges[e].second, { gv::backwards }, g);
  }

  std::ofstream file("graph.dot");
  boost::write_graphviz(file, g, boost::make_label_writer(get(&gv::VertexProperties::name, g)), gv::EdgeColorWriter(g));
#endif

  // Run over all render passes to create them.
  for_each_render_pass(search_forwards,
      [owning_window](RenderPass* render_pass, std::vector<RenderPass*>& UNUSED_ARG(path))
      {
        render_pass->create(owning_window);
        return false;
      });

  owning_window->detect_if_imgui_is_used();
}

void RenderGraph::operator=(RenderPassStream& sink)
{
  // Only assign to each RenderGraph once.
  ASSERT(m_sinks.empty() && m_sources.empty());
  operator+=(sink);
}

void RenderGraph::operator+=(RenderPassStream& sink)
{
  RenderPassStream& source = sink.get_source();
  source.do_load_dont_cares();
  // Add *possible* sources and sinks.
  m_sinks.push_back(sink.owner());
  m_sources.push_back(source.owner());
  // Update incoming and outgoing vertices based on the operator>>'s.
  RenderPass* preceding_node = source.owner();
  for_each_render_pass_from(source.owner(), search_forwards,
      [&](RenderPass* node, std::vector<RenderPass*>& UNUSED_ARG(path))
      {
        node->add_incoming_vertex(preceding_node);
        preceding_node->add_outgoing_vertex(node);
        preceding_node = node;
        return false;
      });
}

#ifdef CWDEBUG

class TestWindow : public task::SynchronousWindow
{
 public:
  TestWindow() : task::SynchronousWindow(nullptr)
  {
    Dout(dc::renderpass, "====================== Next Test ===========================");
  }

  RenderPass lighting{this, "lighting"};
  RenderPass pass1{this, "pass1"};
  RenderPass pass2{this, "pass2"};
  RenderPass render_pass{this, "render_pass"};

  RenderGraph& render_graph() { return m_render_graph; }
  void run_test(std::function<void()> test) { test(); }

  void run_bigtest(vulkan::ImageViewKind const& v1)
  {
    RenderGraph& graph(render_graph());
    Attachment const a1{this, "a1", v1};
    Attachment const a2{this, "a2", v1};
    Attachment const a3{this, "a3", v1};
    Attachment const a4{this, "a4", v1};
    Attachment const a5{this, "a5", v1};
    Attachment const a6{this, "a6", v1};
    Attachment const a7{this, "a7", v1};
    Attachment const a8{this, "a8", v1};
    Attachment const a9{this, "a9", v1};
    Attachment const a10{this, "a10", v1};
    Attachment const a11{this, "a11", v1};
    Attachment const a12{this, "a12", v1};
    RenderPass pass3(this, "pass3");
    RenderPass pass4(this, "pass4");
    RenderPass pass5(this, "pass5");
    RenderPass pass6(this, "pass6");
    RenderPass pass7(this, "pass7");
    RenderPass pass8(this, "pass8");
    RenderPass pass9(this, "pass9");
    RenderPass pass10(this, "pass10");
    RenderPass pass11(this, "pass11");
    RenderPass pass12(this, "pass12");

    graph  = pass1->stores(a1) >> pass2->stores(a2)      >> pass9;
    graph += pass3->stores(a3)                                                                                         >> pass12[+a1][+a9][+a10]->stores(a12);
    graph += pass3             >> pass4[+a3]->stores(a4) >> pass9->stores(a9)                                          >> pass12;
    graph += pass3                                                                    >> pass10[+a3][+a5]->stores(a10) >> pass12;
    graph += pass5->stores(a5) >> pass6->stores(a6)      >> pass7->stores(a7)         >> pass10;
    graph +=                      pass8->stores(a8)      >> pass11[+a6]->stores(a11);
    graph +=                      pass6                  >> pass11;
    graph.generate(nullptr);
  }

 private:
  void create_render_graph() override { }
  void register_shader_templates() override { }
  void create_textures() override { }
  void create_graphics_pipelines() override { }
  void render_frame() override { }
};

#define TEST(test) \
    TestWindow window; \
    Attachment const specular{&window, "specular", v1}; \
    Attachment const output{&window, "output", v1}; \
    RenderGraph& render_graph(window.render_graph()); \
    RenderPass& lighting = window.lighting; \
    RenderPass& pass1 = window.pass1; \
    RenderPass& pass2 = window.pass2; \
    RenderPass& render_pass = window.render_pass; \
    window.run_test([&](){ \
        Dout(dc::renderpass, "Running test: " << #test); \
        render_graph = test; \
        render_graph.generate(nullptr); \
    });

//static
void RenderGraph::testsuite()
{
  constexpr char none = 0;      // char for overloading purposes.
  constexpr int in = 1;
  constexpr int out = 2;
  constexpr int internal = 4;

  constexpr int DONT_CARE = 0;
  constexpr int LOAD = 1;
  constexpr int CLEAR = 2;
  constexpr int STORE = 3;

  vulkan::ImageKind const k1{{}};
  vulkan::ImageViewKind const v1{k1, {}};

  {
    TEST(render_pass->stores(output));                        // attachment not used.
    // specular wasn't used AT ALL - so assign it an index or else has_with will assert.
    specular.assign_unique_index();
    render_graph.has_with(none, specular);
  }
  {
    // Special notation for { DONT_CARE, DONT_CARE }.
    TEST(render_pass[-specular]->stores(output));             // { DONT_CARE, DONT_CARE }
    render_graph.has_with(internal, specular, DONT_CARE, DONT_CARE);
  }
  // Equivalent to:
  {
    TEST(lighting->stores(output) >> render_pass[-specular]->stores(output));       // { DONT_CARE, DONT_CARE }
    render_graph.has_with(internal, specular, DONT_CARE, DONT_CARE);
  }
  {
    TEST(pass1->stores(specular) >> pass2->stores(output) >> render_pass[+specular]->stores(output));             // { LOAD, DONT_CARE }
    render_graph.has_with(in, specular, LOAD, DONT_CARE);
  }
  {
    TEST(render_pass[~specular]->stores(output));             // { CLEAR, DONT_CARE }
    render_graph.has_with(internal, specular, CLEAR, DONT_CARE);
  }
  {
    TEST(render_pass->stores(specular));                      // { DONT_CARE, STORE }
    render_graph.has_with(out, specular, DONT_CARE, STORE);
  }
  {
    bool illegal = false;
    try {
      TEST(render_pass[-specular]->stores(specular));         // AIAlert: Output attachment "specular" is barred. illegal; Can't bar an attachment that wasn't just written.
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    TEST(pass1->stores(specular) >> pass2->stores(output) >> render_pass[+specular]->stores(specular));           // { LOAD, STORE }
    render_graph.has_with(in|out, specular, LOAD, STORE);
  }
  {
    bool illegal = false;
    try {
      TEST(render_pass[~specular]->stores(specular));         // { CLEAR, STORE } or shorter:
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    TEST(render_pass->stores(~specular));                     // { CLEAR, STORE }
    render_graph.has_with(out, specular, CLEAR, STORE);
  }
  {
    bool illegal = false;
    try {
      TEST(render_pass[-specular]->stores(~specular));        // AIAlert: Output attachment "specular" is barred. { CLEAR, STORE }  warning?
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    bool illegal = false;
    try {
      TEST(render_pass[+specular]->stores(~specular));        // illegal (can't have + and ~ at the same time).
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    bool illegal = false;
    try {
      TEST(render_pass[~specular]->stores(~specular));        // { CLEAR, STORE }  warning?
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    TEST(lighting->stores(~specular) >> render_pass->stores(output));               // { LOAD, DONT_CARE }
    render_graph.has_with(in, specular, LOAD, DONT_CARE);
  }
  {
    TEST(lighting->stores(~specular) >> render_pass[-specular]->stores(output));    // attachment not used.
    render_graph.has_with(none, specular);
  }
  {
    bool illegal = false;
    try {
      TEST(lighting->stores(~specular) >> render_pass[+specular]->stores(output));  // { LOAD, DONT_CARE } - but over specified. Lets not accept this.
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    bool illegal = false;
    try {
      TEST(lighting->stores(~specular) >> render_pass[~specular]->stores(output));  // illegal; makes no sense to clear an attachment that was just written to.
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    TEST(lighting->stores(~specular) >> render_pass->stores(specular));             // { LOAD, STORE }
    render_graph.has_with(in|out, specular, LOAD, STORE);
  }
  {
    bool illegal = false;
    try {
      TEST(lighting->stores(~specular) >> render_pass[-specular]->stores(specular));// illegal; makes no sense to write to an attachment that was just written to.
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    bool illegal = false;
    try {
      TEST(lighting->stores(~specular) >> render_pass[+specular]->stores(specular));// { LOAD, STORE } - but over specified. Lets not accept this.
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    bool illegal = false;
    try {
      TEST(lighting->stores(~specular) >> render_pass[~specular]->stores(specular));// illegal; makes no sense to clear an attachment that was just written to.
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    bool illegal = false;
    try {
      TEST(lighting->stores(~specular) >> render_pass->stores(~specular));          // illegal; makes no sense to clear an attachment that was just written to.
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    bool illegal = false;
    try {
      TEST(lighting->stores(~specular) >> render_pass[-specular]->stores(~specular));// illegal; makes no sense to clear an attachment that was just written to.
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    bool illegal = false;
    try {
      TEST(lighting->stores(~specular) >> render_pass[+specular]->stores(~specular));// illegal; makes no sense to clear an attachment that was just written to.
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    bool illegal = false;
    try {
      TEST(lighting->stores(~specular) >> render_pass[~specular]->stores(~specular));// illegal; makes no sense to clear an attachment that was just written to.
    } catch (AIAlert::Error const& error) {
      Dout(dc::renderpass, "Illegal: " << error);
      illegal = true;
    }
    ASSERT(illegal);
  }
  {
    TestWindow window;
    window.run_bigtest(v1);
  }
  {
    TEST(lighting->stores(~specular) >> render_pass->stores(output) >> pass1[+specular]->stores(output));
    render_graph.has_with(in, specular, LOAD, STORE, &render_pass);
  }

  DoutFatal(dc::fatal, "RenderGraph::testuite successful!");
}

void RenderGraph::has_with(char, Attachment const& attachment) const
{
  ASSERT(m_sinks.size() == 1);
  RenderPass const* render_pass = m_sinks[0];
  // Attachment not used.
  ASSERT(!render_pass->is_known(&attachment));
  // Nothing should be listed in m_remove_or_dontcare_attachments.
  ASSERT(render_pass->remove_or_dontcare_attachments_empty());
}

void RenderGraph::has_with(int in_out, Attachment const& attachment, int required_load_op, int required_store_op, RenderPass const* render_pass) const
{
  if (render_pass == nullptr)
  {
    ASSERT(m_sinks.size() == 1);
    // The render pass under test is always the last render pass.
    render_pass = m_sinks[0];
  }

  // Nothing should be listen in m_remove_or_dontcare_attachments.
  ASSERT(render_pass->remove_or_dontcare_attachments_empty());

  // Same as RenderGraph::testsuite.
  constexpr int in = 1;
  constexpr int out = 2;
  constexpr int internal = 4;

  constexpr int DONT_CARE = 0;
  constexpr int LOAD = 1;
  constexpr int CLEAR = 2;
  constexpr int STORE = 3;

  // Use `none` for the zero value.
  ASSERT(in_out);
  // Only use `internal` on its own.
  ASSERT(in_out == internal || !static_cast<bool>(in_out & internal));

  auto load_op = render_pass->get_load_op(&attachment);
  auto store_op = render_pass->get_store_op(&attachment);

  vk::AttachmentLoadOp rq_load_op = required_load_op == LOAD ? vk::AttachmentLoadOp::eLoad : required_load_op == CLEAR ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eDontCare;
  vk::AttachmentStoreOp rq_store_op = required_store_op == STORE ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;

  ASSERT(load_op == rq_load_op);
  ASSERT(store_op == rq_store_op);
}

#endif // CWDEBUG

} // namespace vulkan::rendergraph

#if defined(CWDEBUG) && !defined(DOXYGEN)
NAMESPACE_DEBUG_CHANNELS_START
channel_ct renderpass("RENDERPASS");
channel_ct rpverbose("RPVERBOSE");
NAMESPACE_DEBUG_CHANNELS_END
#endif
